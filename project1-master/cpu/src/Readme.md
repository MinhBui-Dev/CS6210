
# Intro

This is a simple scheduling program that aims to balance guests' computing loads across all available physical cpu from the host.

# Requirement and Assumption

## Requirement

1. No PCPU should be under/over utilize, within 5% of standard deviation across all PCPU utilization for balanced and stable schedule.
2. No involvement if the loads are balanced across all PCPU already.
3. Scheduler should be able to repin each VCPU on different PCPUs as needed.
4. Scheduler should not make any unnecessary repin once balanced schedule is established.
5. Scheduler shoud be able to handle changes in VMs' loads halfway through.
6. Scheduler should be able to run regardless the number of VCPU vs PCPU, i.e VCPU > PCPU, VCPU < PCPU, VCPU = PCPU.

## Assumption

1. VCPU affinity to multiple PCPU but costs of context switch and cache invalidation lead to VPU to run on a single PCPU
2. PCPU usage per interval = total amount of times all Vcpu pinned to such PCPU

# Useful Statistical Data

## VCPU time

Libvirt domain provides cumulative time used by each individual VCPU in the `struct virVcpuInfo` in nanoseconds.Thus we need to keep track of the previous cumulative time between each interval so we can determined how much time the VCPU spends on PCPU.


## VCPU to PCPU pinning

`struct virVcpuInfo` also provides the info on which PCPU a particular VCPU is pinned on.

## Number of Guess Domains and available PCPUs

Provided by `virConnectListAllDomains()`  and  `virNodeGetInfo()` API calls.

# Logic and Implementation

## User defined data structure

```c
    struct DomainLoad
    {
        int index;          // Domain Index
        float usage;        // Calculated domain usage
        int pcpuIndex;      // Pcpu that we will pin Vcpu on
        int pcpuPrevIndex;  // Pcpu that Vcpu is currently running on
    };
```

##  Determine domain's loads

By keeping track of CPU time used at beginning and end of sleep interval, we can calculate the time used by the VCPU:

    DomainLoad = (CurrentTime - PreviousTime)*100/time // % utilization

This value is then saved in the `usage` member of struct `DomainLoad`.

## Determine PCPU's utilization

PCPU's utilization is calculated by adding each domain's usage accordingly to the current pinned PCPU (i.e. `pcpuPrevIndex`).

```c
    pcpusUtilization[(domainLoadPtr + i)->pcpuPrevIndex] += (domainLoadPtr + i)->usage;
```

Using the array of pcpuUtilization, we can get the standard deviation of the population and determine if there is a need for balancing loads.

## Determine balanced schedule
In order to determine the balanced schedule, we need to sort the array of domainLoadPtr in a descending order of their usage. This is done by using the `qsort()` function.

```c
    qsort(domainLoadPtr, nDomain, sizeof(struct DomainLoad), cmp);
```

The `cmp` function is a comparator function that compares the `usage` member of two `DomainLoad` struct.

```c
    int cmp(const void *a, const void *b)
    {
        struct DomainLoad *a1 = (struct DomainLoad *)a;
        struct DomainLoad *a2 = (struct DomainLoad *)b;
        if ((*a1).usage > (*a2).usage)
            return -1;
        else if ((*a1).usage < (*a2).usage)
            return 1;
        else
            return 0;
    }
```

After sorting, we can determine a balanced schedule using the greedy partition algorithm. The algorithm is as follows:

- Number of PCPUs = number of bucket
- Find the smallest bucket, if  all buckets are equal or empty, pick the first one.
- Pin the VCPU to the PCPU of the smallest bucket, saving the bucket index in the `pcpuIndex` member of struct `DomainLoad`.
- Find the smallest bucket again and repeat until all VCPU are pinned.

## Repin VCPU
Pinning VCPU to PCPU is done by using the `virDomainPinVcpu()` API call. We first set the cpumap for each VCPU using `memset()` to 0 bit. Then we set the bit of the PCPU that we want to pin the VCPU to.

```c
    memset(cpumap, 0, maplen);
	VIR_USE_CPU(cpumap, pinPcpu);
```

Then we call the `virDomainPinVcpu()` API call to pin the VCPU to the PCPU.

```c
    if (virDomainPinVcpu(domainList[domainIndex], 0, cpumap, maplen) == -1)
	{
		printf("Failed to pin vcpu to pcpu for domain # %d\n", domainIndex);
	}
```

Once a balanced utilization rate across all PCPU has been established, the scheduler should only be checking the standard deviation every time it runs and leave the pinning alone.

# Additional Consideration

The simple VCPU_scheduler aims to produce balance utilization rate across all physical cpu of the host. In real life situation, we should also take into account of the priority of the load, the responsiveness for the guest, and the throughput of the guest into scheduling policy.



