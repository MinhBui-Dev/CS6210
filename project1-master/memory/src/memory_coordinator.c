#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

struct domainMemUsage{
	unsigned long long actual;
	unsigned long long unused;
	long memIncr;
	long memDecr;
	long memConsumption;
	int memReclaimedBool;
	int memDemandingBool;
	int memReleasedBool;
};

struct domainMemUsage *prevUsage = NULL;
int allocatedMem = 0;


void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);

	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{

	int statPeriod = interval;
	int memoryIncrement = 100; //MB
	unsigned long long unusedMin = 100; //MB
	unsigned long long actualMin = 200; //MB
	unsigned long long maxMem = 2048; //MB
	unsigned long long hostMin = 200; //MB



	// get list of domain
	virDomainPtr *domainList;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING;

	int nDomain = virConnectListAllDomains(conn,&domainList,flags); //return the list of running domains

	if ( nDomain < 0)
	{
		printf("Failed to get list of domains.\n");
		return;
	}

	struct domainMemUsage *memUsage = calloc(sizeof(struct domainMemUsage),nDomain);

	for (size_t i = 0; i < nDomain; i++)
	{
		//Set Stat Period
		if (virDomainSetMemoryStatsPeriod(domainList[i],statPeriod,VIR_DOMAIN_AFFECT_LIVE)==-1)
		{
			printf("Failed to set stats period for domain[%ld].\n",i);
		}


		virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int nr_stats;
		nr_stats = virDomainMemoryStats(domainList[i],stats,VIR_DOMAIN_MEMORY_STAT_NR,0);
		if (nr_stats == -1)
		{
			printf("Error getting memory stats for domain[%ld].\n",i);
		}

		//printf("Domain[%ld] -->",i);
		for (size_t j = 0; j < nr_stats; j++)
		{
			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
			{
				(memUsage+i)->unused = stats[j].val/1024;
				//printf("unused %llu--", (memUsage+i)->unused);
			}

			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
			{
				(memUsage+i)->actual = stats[j].val/1024;
				//printf("actual %llu--", (memUsage+i)->actual);
			}
		}
	}

	if (prevUsage == NULL)
	{

		for (size_t i = 0; i < nDomain; i++)
		{
			(memUsage+i)->memReclaimedBool = 0;
			(memUsage+i)->memDemandingBool = 0;
			(memUsage+i)->memReleasedBool = 0;
			(memUsage+i)->memIncr = 0;
			(memUsage+i)->memDecr = 0;
			(memUsage+i)->memConsumption = 0;

		}

		prevUsage = memUsage;
		memUsage = NULL;

		//printf("return from prevUsage\n");
		//printf("\n");
		return;
	}

	//printf("Continue with current usage\n");
	int needMemoryBoolean[nDomain];
	long memoryConsumptionRate[nDomain];
	memset(&needMemoryBoolean,0,sizeof(int)*nDomain);


	int releaseMemory[nDomain];
	memset(&releaseMemory,0,sizeof(int)*nDomain);

	for (size_t i = 0; i < nDomain; i++)
	{
		// unsigned long long deltaUsage = (prevUsage + i)->unused - (memUsage + i)->unused;
		// memUsed = (memUsage + i)->actual - (memUsage + i)->unused;
		// memPrevUsed = (prevUsage + i)->actual - (prevUsage + i)->unused;

		printf("Domain[%ld]---prev: actual[%llu]/unused[%llu]---curr: actual[%llu]/unused[%llu]\n",i,(prevUsage + i)->actual,(prevUsage + i)->unused,(memUsage + i)->actual,(memUsage + i)->unused);
		// Determine if domain is demanding memory
		long dUnused = ((prevUsage + i)->unused - (memUsage+i)->unused)/1;
		long dActual = ((prevUsage + i)->actual - (memUsage+i)->actual)/1;
		(memUsage+i)->memConsumption = (prevUsage+i)->memConsumption + dUnused;

		//printf("Domain[%ld]---> dActual[%d] --- dUnused[%d] --- (prevUsage+i)->memIncr[%lld]\n",i, dActual, dUnused, -((prevUsage+i)->memIncr));
		//printf("[dUnused < -((prevUsage+i)->memIncr)]: %d && [dActual == 0]: %d\n",dUnused < -((prevUsage+i)->memIncr),dActual == 0);

		// if prevConsumpt + dUnused > prevConsumpt -> Demanding memory
		// dActual < 0 -> New memory allocated
		// dUnused > 0 -> Consuming memory
		// dUnused < 0 -> memory released || memory allocated

		if ((dUnused > 5 && dActual == 0) || (dActual < 0 && (prevUsage+i)->memDemandingBool == 1))
		{
			(memUsage+i)->memDemandingBool = 1;
			memoryConsumptionRate[i] = dUnused; //Save current consumption rate
			printf("Domain[%ld] is demanding memory\n",i);
		}


		// Determine if domain's process releasing memory
		else if	((  -dUnused > memoryIncrement && dActual == 0 && allocatedMem !=0)  || (prevUsage+i)->memReleasedBool==1)
		{
			(memUsage+i)->memReleasedBool = 1;
			releaseMemory[i]= 1;

			printf("Domain[%ld] is releasing memory\n",i);
		}
		else
		{
			(memUsage+i)->memDemandingBool = 0;
			(memUsage+i)->memReleasedBool = 0;
			releaseMemory[i]= 0;
			memoryConsumptionRate[i] = 0;
		}


		//printf("Delta usage for domain[%ld]: %llu --->",i,deltaUsage);
		if ((memUsage+i)->memDemandingBool == 1 && memUsage[i].unused <unusedMin )
		{
			needMemoryBoolean[i] = 1;
			//printf("Domain[%ld] needs memory\n",i);
		}
		else
		{
			needMemoryBoolean[i] = 0;
			//printf("Domain[%ld] does not need memory\n",i);
		}
	}

	//check if there is domain releasing mem
	int freeMem = 0;
	for (size_t i = 0; i < nDomain; i++)
	{
		freeMem += releaseMemory[i];
	}

	if (freeMem > 0)
	{
		for (size_t i = 0; i < nDomain; i++)
		{
			if (releaseMemory[i] == 1 && memUsage[i].actual > actualMin && (prevUsage+i)->memReclaimedBool == 0)
			{
				int memDecrement = MIN(memoryIncrement, memUsage[i].unused -unusedMin);
				if (virDomainSetMemory(domainList[i], MAX(memUsage[i].actual - memDecrement, actualMin) * 1024) == -1)
				{
					printf("Failed to reclaimed memory from domain[%ld].\n", i);
				}
				else
				{
					printf("Reclaimed from idle memory domain[%ld]\n",i);
					(memUsage+i)->memReleasedBool = 1;
				}
			}
			else
			{
				(memUsage+i)->memReleasedBool = 0;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < nDomain; i++)
		{
			(memUsage+i)->memReleasedBool = 0;
		}
	}



	//check if there is a need for memory
	int needMem = 0;
	for (size_t i = 0; i < nDomain; i++)
	{
		needMem += needMemoryBoolean[i];
	}



	if (needMem > 0)
	{
		// reclaiming memory
		unsigned long long memReclaimedRate[nDomain];
		// get Host Free Memory

		unsigned long long hostMem = (virNodeGetFreeMemory(conn)/1024)/1024; //Convert to MB

		for (size_t i = 0; i < nDomain; i++)
		{
			if (prevUsage[i].memDemandingBool == 0 && memUsage[i].actual > actualMin && memUsage[i].unused > unusedMin)
			{
				int memDecrement = MIN(memoryIncrement, memUsage[i].unused - unusedMin);
				if (virDomainSetMemory(domainList[i], MAX((memUsage[i].actual - memDecrement), actualMin) * 1024) == -1)
				{
					printf("Failed to reclaimed memory from domain[%ld].\n", i);
				}
				else
				{
					memReclaimedRate[i] = MIN(memDecrement, memUsage[i].actual - actualMin);
					printf("Mem reclaimed from domain[%ld]: %llu\n", i, memReclaimedRate[i]);
					memUsage[i].memReclaimedBool = 1;
				}
			}
			else
			{
				memReclaimedRate[i] = 0;
				memUsage[i].memReclaimedBool = 0;
			}
		}

		unsigned long long memReclaimedTotal = 0;

		for (size_t i = 0; i < nDomain; i++)
		{
			memReclaimedTotal += memReclaimedRate[i];
		}

		printf("Total mem reclaimed: %llu\n", memReclaimedTotal);

		long memAvailableTotal = MAX((hostMem - hostMin)+memReclaimedTotal,0);

		long totalConsumption = 0;
		for (size_t i = 0; i < nDomain; i++)
		{
			totalConsumption += memoryConsumptionRate[i];
		}

		long memToAllocate = memAvailableTotal;

		// if (memReclaimedTotal > (1.5*totalConsumption))
		// {
		// 	memToAllocate = memReclaimedTotal;
		// }
		// else
		// {
		// 	memToAllocate = memAvailableTotal;
		// }


		printf("Host Memory[%llu] --- MemToAllocate[%ld]\n",hostMem,memToAllocate);
		// Check partition vs need vs max host
		unsigned long long memPartition = memToAllocate/needMem;

		memPartition = MIN(memPartition,memoryIncrement);

		for (size_t i = 0; i < nDomain; i++)
		{
			if (needMemoryBoolean[i] == 1 && memUsage[i].actual <= maxMem)
			{
				if (virDomainSetMemory(domainList[i], MIN(memUsage[i].actual + memPartition, maxMem) * 1024) == -1)
				{
					printf("Failed to allocated memory from domain[%ld].\n", i);
				}
				else
				{
					(memUsage+i)->memIncr = MIN(memPartition,memUsage[i].actual - maxMem);
				}

				allocatedMem = 1;
			}
		}
	}
	else
	{
		for (size_t i = 0; i < nDomain; i++)
		{
			(memUsage+i)->memReclaimedBool = 0;
		}
	}
	free(prevUsage);
	prevUsage = memUsage;
	memUsage = NULL;
	printf("\n");
}
