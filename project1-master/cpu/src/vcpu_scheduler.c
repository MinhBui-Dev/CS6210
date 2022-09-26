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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE


double *domainPreviousUsage = NULL;

struct DomainLoad
{
	int index;
	float usage;
	int pcpuIndex;
	int pcpuPrevIndex;
};

//Helper functions used to compare and sort domainLoad structures
int cmp(const void *a, const void *b);
int min(const void *a,int size);


void CPUScheduler(virConnectPtr conn,int interval);

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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	double time = interval * pow(10, 9);
	// first get host info
	virNodeInfo host;
	if (virNodeGetInfo(conn, &host) == -1)
	{
		printf("Failed to get host info\n");
	}

	int npcpu = VIR_NODEINFO_MAXCPUS(host); // number of physical cpu
	printf("Number of PCPU using virNodGetInfo: %d\n", npcpu);

	// get list of domain
	virDomainPtr *domainList;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING;

	int nDomain = virConnectListAllDomains(conn, &domainList, flags); // return the list of running domains

	if (nDomain < 0)
	{
		printf("Failed to get list of domains.\n");
		return;
	}

	// Getting start time interval usage
	double *domainUsage = calloc(sizeof(double), nDomain);
	struct DomainLoad *domainLoadPtr = calloc(sizeof(struct DomainLoad), nDomain);

	for (size_t i = 0; i < nDomain; i++)
	{
		// get domain info
		virDomainInfo domainInfo;
		if (virDomainGetInfo(domainList[i], &domainInfo) == -1)
		{
			printf("Failed to get domain info for domain # %ld\n", i);
		}

		int nvcpu = domainInfo.nrVirtCpu;
		// printf("Domain %ld has %d vcpu.\n",i,nvcpu);

		int maplen = VIR_CPU_MAPLEN(npcpu);
		unsigned char *cpumap = calloc(nvcpu, maplen);
		virVcpuInfoPtr vcpuInfo = malloc(sizeof(virVcpuInfo) * nvcpu);
		if (virDomainGetVcpus(domainList[i], vcpuInfo, nvcpu, cpumap, maplen) == -1)
		{
			printf("Failed to get vcpu info for domain # %ld\n", i);
		}

		// Print statement testing return from virDomainGetVcpus()
		// printf("Domain[%ld] Time used by VCPU: [%lld] ---> PCPU: [%d]\n",i,(vcpuInfo->cpuTime),vcpuInfo->cpu);
		// printf("CPUMAP for VCPU[%d]: %x\n",vcpuInfo->number,*cpumap);

		domainUsage[i] = (vcpuInfo->cpuTime);
		(domainLoadPtr + i)->pcpuPrevIndex = vcpuInfo->cpu;

		free(vcpuInfo);
		free(cpumap);
	}

	// intialize domainPreviousUsage
	if (domainPreviousUsage == NULL)
	{
		// printf("first iteration with no prior usage info\n");
		domainPreviousUsage = domainUsage;
		domainUsage = NULL;

		for (size_t i = 0; i < nDomain; i++)
		{
			virDomainFree(domainList[i]);
		}

		free(domainList);
		free(domainLoadPtr);
		return;
	}

	double *pcpusUtilization = calloc(sizeof(double), npcpu);

	for (size_t i = 0; i < nDomain; i++)
	{
		(domainLoadPtr + i)->usage = (domainUsage[i] - domainPreviousUsage[i]) * 100 / time;
		(domainLoadPtr + i)->index = i;
		// printf("CPU Usage for domain[%d] is: %f.\n", (domainLoadPtr + i)->index, (domainLoadPtr + i)->usage);
		pcpusUtilization[(domainLoadPtr + i)->pcpuPrevIndex] += (domainLoadPtr + i)->usage;
	}

	for (size_t i = 0; i < npcpu; i++)
	{
		printf("PCPU[%ld] utilzation: %f\n", i, pcpusUtilization[i]);
	}

	// Calculate standard deviation
	double sum = 0.0, mean = 0.0, SD = 0.0;
	int i;
	for (i = 0; i < npcpu; ++i)
	{
		sum += pcpusUtilization[i];
	}
	mean = sum / npcpu;
	for (i = 0; i < npcpu; ++i)
	{
		SD += pow(pcpusUtilization[i] - mean, 2);
	}
	SD = sqrt(SD / npcpu);
	printf("Standard Deviation: %f\n", SD);

	// pin VCPU to PCPU if StdD is higher than 5%
	if (SD > 5)
	{

		// Sort in descending order of load
		qsort(domainLoadPtr, nDomain, sizeof(struct DomainLoad), cmp);

		// Print statement testing sorting
		/* printf("Sorted CPU for each domain\n");

		for (size_t i = 0; i < nDomain; i++)
		{
			printf("CPU Usage for domain[%d] is: %f.\n", (domainLoadPtr + i)->index, (domainLoadPtr + i)->usage);
		} */

		double *bucketArray = calloc(sizeof(double), npcpu);
		for (size_t i = 0; i < nDomain; i++)
		{
			int smallestBucketIndex = min(bucketArray, npcpu);
			*(bucketArray + smallestBucketIndex) += (domainLoadPtr + i)->usage;
			// printf("Domain[%d] is pinned to PCPU[%d]\n", (domainLoadPtr + i)->index, smallestBucketIndex + 1);
			(domainLoadPtr + i)->pcpuIndex = smallestBucketIndex;
		}

		for (size_t i = 0; i < npcpu; i++)
		{
			printf("PCPU[%ld] Estimated Utilization: %f\n", i + 1, *(bucketArray + i));
		}

		// Pin VCPU to CPU
		for (size_t i = 0; i < nDomain; i++)
		{
			// get index for domainList, pcpu number from domainLoadPtr struct
			int domainIndex = (domainLoadPtr + i)->index;
			int pinPcpu = (domainLoadPtr + i)->pcpuIndex;

			// get domain info
			virDomainInfo domainInfo;
			if (virDomainGetInfo(domainList[domainIndex], &domainInfo) == -1)
			{
				printf("Failed to get domain info for domain # %d\n", domainIndex);
			}

			int nvcpu = domainInfo.nrVirtCpu;

			int maplen = VIR_CPU_MAPLEN(npcpu);
			unsigned char *cpumap = calloc(nvcpu, maplen);
			memset(cpumap, 0, maplen);

			VIR_USE_CPU(cpumap, pinPcpu);

			if (virDomainPinVcpu(domainList[domainIndex], 0, cpumap, maplen) == -1)
			{
				printf("Failed to pin vcpu to pcpu for domain # %d\n", domainIndex);
			}

			// printf("Domain[%d]: VCPU pinned to PCPU[%d] with bitmap: %x.\n",domainIndex,pinPcpu,*cpumap);
			free(cpumap);
			virDomainFree(domainList[domainIndex]);
		}

		// free calloc
		free(bucketArray);
	}

	// Free previous usage memory region and point to current usage
	free(pcpusUtilization);
	free(domainLoadPtr);
	free(domainPreviousUsage);
	free(domainList);
	domainPreviousUsage = domainUsage;
	domainUsage = NULL;
}

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

int min(const void *a, int size)
{
	int i, minIndex = 0;
	double *sumArray = (double *)a;
	for (i = 1; i < size; i++)
	{
		if (*(sumArray + i) < *(sumArray + minIndex))
		{
			minIndex = i;
		}
	}
	return minIndex;
}
