## Testing on Azure

In this guide, we will explore how to test this project on Microsoft Azure.

### 1 - Getting free Azure credits with "Azure for Students (Optional)"

Azure offers $100 in credits to students when they sign up with the university email. With proper techniques for 
managing VM costs such as deallocation discussed below, this should be enough to test project 1. To sign up for Azure
for Students, please check out this link: https://azure.microsoft.com/en-us/free/students.

### 2 - Selecting an instance type

The first step is to select a virtual machine with at least 8 vCPUs and 16 GB RAM that supports nested virtualization.
This [page](https://docs.microsoft.com/en-us/azure/virtual-machines/acu) lists all Azure compute units, and machines
that support nested virtualization are indicated with a triple asterisk. We recommend the **D4s_v3** instance type.

### 3 - Setting up the instance

Before creating our instance, you will need to have the Azure CLI installed on your computer. Find the appropriate guide
for your operating system here - https://docs.microsoft.com/en-us/cli/azure/install-azure-cli.

The first step is to create a resource group for our instance and its resources. Run the following command to create a resource group:

```shell
az group create --name 'pr1-vm-rg' --location 'eastus'
```

Now, run this command to create the VM itself:

```shell
az vm create \
--name pr1-vm \
--resource-group pr1-vm-rg \
--size Standard_D4s_v3 \
--location eastus \
--image Canonical:0001-com-ubuntu-server-focal:20_04-lts:latest \
--admin-username azureuser \
--generate-ssh-keys \
--public-ip-sku Standard \
--verbose
```

If successful, you will get a JSON output like this:

```json
{
  "fqdns": "",
  "id": "/subscriptions/71802f2f-0977-4666-b8e2-d9f87beff78e/resourceGroups/pr1-vm-rg/providers/Microsoft.Compute/virtualMachines/pr1-vm",
  "location": "eastus",
  "macAddress": "00-0D-3A-9D-B8-03",
  "powerState": "VM running",
  "privateIpAddress": "10.0.0.4",
  "publicIpAddress": "20.63.151.22",
  "resourceGroup": "pr1-vm-rg",
  "zones": ""
}
```

To connect to the VM, run the command below, replacing `PUBLIC-IP-ADDRESS` with the `publicIpAddress` in the JSON output from VM creation:

```shell
ssh azureuser@PUBLIC-IP-ADDRESS
```

You should now be logged in to the virtual machine, and can proceed to setup project 1 on it for testing.

Now, we will install the necessary dependencies for our project. Run this command:

```shell
  sudo apt-get -y update && \
  DEBIAN_FRONTEND=noninteractive sudo apt-get -y -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" upgrade && \
  DEBIAN_FRONTEND=noninteractive sudo apt-get -y install \
  build-essential \
  qemu-kvm \
  uvtool \
  libvirt-dev \
  libvirt-daemon-system \
  python3-libvirt \
  python-is-python3 \
  python3-pandas \
  python3-matplotlib \
  virt-top \
  virt-manager \
  tmux \
  git \
  zip \
  unzip && \
  sudo uvt-simplestreams-libvirt sync release=bionic arch=amd64
```

Next, update the SSH key config so we can easily access the nested VMs when they are created. Run the following command:

```shell
  echo "Host 192.168.*" > $HOME/.ssh/config && \
  echo -e "\tStrictHostKeyChecking no" >> $HOME/.ssh/config && \
  echo -e "\tUserKnownHostsFile /dev/null" >> $HOME/.ssh/config && \
  echo -e "\tLogLevel ERROR" >> $HOME/.ssh/config && \
  ssh-keygen -t rsa -q -f "$HOME/.ssh/id_rsa" -N ""
```

### 4 - Testing the project

To test your implementation on the instance, you need to download your code onto the instance. If you're already using a
private Git repository for your work (which is highly recommended), you can clone your project on the instance with `git`.
Otherwise, you can use `SFTP` or any other convenient method.

Once you have cloned your project, you can proceed to run the CPU and memory tests on it.

### 5 - Deallocating an instance

In order to avoid burning our student credits (or personal funds) unnecessarily, we can deallocate our instance when it is not
actively in use. Please note that on Azure, merely stopping an instance does not stop you from incurring costs on other instance resources
such as storage. For this reason, we need to deallocate the instance so that we free these resources.

To deallocate an instance, run this command:

```shell
az vm deallocate --name pr1-vm --resource-group pr1-vm-rg
```

### References

1. [Azure compute unit (ACU)](https://docs.microsoft.com/en-us/azure/virtual-machines/acu)
2. [Azure for Students](https://azure.microsoft.com/en-us/free/students)
3. [How to install the Azure CLI](https://docs.microsoft.com/en-us/cli/azure/install-azure-cli)
