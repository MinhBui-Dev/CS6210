
# Intro

A memory coordinator that aims to provide necessary memory to the VMs and release memory when not needed.

# Requirement

1. Make sure that the VMs and the host has sufficient memory after any release of memory.
2. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
3. Domain should not release memory if it has less than or equal to ***100MB*** of unused memory.
4. Host should not release memory if it has less than or equal to ***200MB*** of unused memory.

# Useful Statistical Data

## Domain available and used memory

`VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON` - The actual balloon value (in KiB) from `virDomainMemoryStats`. This number is the max value in which the balloon can be inflated to or deflated from. When the balloon driver detects that the domain is asking for more memory, it will deflate the balloon from this value until the balloon reaches 0. when the domain is no longer asking for memory, the balloon will inflate back to this value. Thus, this value is effectively the total amount of memory the domain is allowed to use.

`VIR_DOMAIN_MEMORY_STAT_UNUSED` - This is the amount of memory left completely unused by the system. We can use this value to determine when to allocate more memory to a domain, when to release memory from a domain, and when to reclaim memory from a domain.

## Host free memory

Host free memory can be obtained by API call to `virNodeGetFreeMemory()`. this returns the amount of free memory (in byte) on the host.

## Memory consumption rate of a domain

We calculate the memory consumption of a domain in a time interval by the following formula:

    memory_consumption_rate = prevUnused - currUnused

# Policy

## Memory allocation

- Memory should come from idle domains first before releasing memory from the host.
- If there is no idle domain, then memory should be released from the host.
- Memory increase should take into account of the memory usuage of the domain. for example, if a domain is using 150 MB  per second of memory, while the standard increase is 100 MB, the memory coordinator should try to allocate more than at least 1.5x the memory usage, if allowed by the host.
- No memory should be allocated if the host has less than 200 MB of memory.

## Memory release

- Memory should not be released from a domain that is also using memory.
- Domain with unused memory < 100 MB or actual memory < 200 MB should not release memory.

## Memory reclaim

- Memory reclaim is for when the VM stops using the memory allocated to it and thus its memory can be released to the host.
- Memory reclaim is determined by # intervals of inactivity usage as well as the amount of unused memory above the threshold.
- Memory reclaim stop around 400 MB of actual memory. This is so that a domain can have a buffer of memory to use when it is needed, preventing the domain from having to request memory from the host again.


# Logic and Implementation

## User defined data structure

```c
struct domainMemUsage{
    unsigned long long actual;
    unsigned long long unused;
    int memDemandingBool;
    int memFreeCount;
};
```

## Determine if a domain is demanding memory

We determine if a domain is consuming memory by the change in `unused` memory between current value and previous value. We also take in account of when more memory is allocated to the domain. in this case, we will check if the domain is demanding memory previously and if the actual memory has increased.

```c
    long dUnused = ((prevUsage + i)->unused - (memUsage + i)->unused) / 1;
    if ((dUnused > 5 && dActual == 0)
    || (dActual < 0 && (prevUsage + i)->memDemandingBool == 1))
	{
		(memUsage + i)->memDemandingBool = 1;
		memoryConsumptionRate[i] = dUnused; // Save current consumption rate
    }
```

## Determine if a domain is idle

We determine if a domain is idle by checking if the `unused` memory is above the threshold and if the change between the current and previous `unused` memory is less than 5 MB. However, we will count the number of interval in which the domain satisfies the condition. If the number of interval is above the threshold, then the domain is considered idle and we can start reclaiming memory from it. Once we set the domain as a candidate for memory reclaim, we will reset the counter as we don't want to reclaim memory consecutively.

```c
	if ((memUsage + i)->unused > unusedThreshold
    && (prevUsage + i)->unused > unusedThreshold && dUnused < 5)
	{
		(memUsage + i)->memFreeCount = (prevUsage + i)->memFreeCount + 1;
		if ((memUsage + i)->memFreeCount == 3)
		{
			// set flag to reclaimed memory
			idleDomain[i] = 1;

			// reset count
			(memUsage + i)->memFreeCount = 0;
			printf("Domain[%ld] is releasing memory\n", i);
		}
	}
	else
	{
		(memUsage + i)->memFreeCount = 0;
	}
```

## Determine domain to release memory from

We determine the domain to release memory from by checking if the domain is demanding memory and if the memory limit on the domain satisfied (i.e `unused` memory > 100 MB or `actual` memory > 200MB). we also need to compare the memory available for release and memory increment of 100 MB, using whichever value that is smaller.

```c
    if (memUsage[i].memDemandingBool == 0 && prevUsage[i].memDemandingBool == 0
    && memUsage[i].actual > actualMin && memUsage[i].unused > unusedMin)
	{
		int memDecrement = MIN(memoryIncrement, memUsage[i].unused - unusedMin);
		if (virDomainSetMemory(domainList[i], MAX((memUsage[i].actual - memDecrement), actualMin) * 1024) == -1)
		{
			printf("Failed to reclaimed memory from domain[%ld].\n", i);
		}
	}
```

## Determine amount of memory to allocate

We only need to allocate memory when a domain is demanding memory and the `unused` memory for this domain is lower than the threshold. We also need to check whether the host has available free memory above the 200 MB threshold. the amount of memory to allocate takes into account the maximum consumption rate of any domain and the memory increment of 100 MB, using whichever value that is higher. However, if the amount of memory available from the host is less than the amount of memory to allocate, we will only allocate the amount of memory available from the host. The max memory limit for a domain is 2 GB.

```c
    if (memAvailableTotal > hostMin)
	{
		// Check partition vs need vs max host
		unsigned long long memPartition = (memAvailableTotal - hostMin) / nDomainNeedMem;
		printf("Host Memory[%lu] --- MemToAllocate[%ld]\n", memAvailableTotal, (memAvailableTotal - hostMin));

		long maxConsumption = 0;
		for (size_t i = 0; i < nDomain; i++)
		{
			maxConsumption = MAX(maxConsumption,memoryConsumptionRate[i]);
		}

		memoryIncrement = MAX(memoryIncrement,1.5*maxConsumption);
		memPartition = MIN(memPartition, memoryIncrement);
		for (size_t i = 0; i < nDomain; i++)
		{
			if (needMemoryBoolean[i] == 1 && memUsage[i].actual <= maxMem)
			{
				if (virDomainSetMemory(domainList[i], MIN(memUsage[i].actual + memPartition, maxMem) * 1024) == -1)
				{
					printf("Failed to allocated memory from domain[%ld].\n", i);
				}
			}
		}
	}
```

