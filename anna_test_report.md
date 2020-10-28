# Overview
In recent years, cloud infrastructure, which can provide the ability to scale across machines and global, has become mainstream. Since we do not have a proper cloud-native NoSQL store to support various application needs. And Anna is a low-latency, autoscaling key-value store developed in the [RISE Lab](https://rise.cs.berkeley.edu/) at [UC Berkeley](https://www.berkeley.edu/). Therefore, we want to dive deeper into Anna and verify if it is the right candidate to slot it into our "data management" domain.

To better understand Anna, we started with the two papers, "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)" and "[Autoscaling Tiered Cloud Storage in Anna](https://dsf.berkeley.edu/jmh/papers/anna_vldb_19.pdf)", published by UC Berkeley. Since we care about performance and scalability at any scale, we designed two types of experiment to test Anna in different situations.

- **Single node multi-core experiment**. Through this experiment, we wanted to understand the coordination-free actor model, lattice-based composite data structures, the state model, and configurations that Anna presents. Besides, we paid more attention to comparing Anna's performance with Redis because Anna claims that its performance far exceeds the state of the art.

- **Distributed experiments**. In these experiments, we focused on performance and scalability at any scale.

# Single node multi-core experiment

**Testing environments**

||Figure 1|Figure 2/3|
|:----|:----:|:----:|
|Anna Server|AWS EC2 m4.16xlarge instance |Local Server(80  Intel(R) Xeon(R) CPU E7- 4870  @ 2.40GHz) |
|Redis |AWS EC2 m4.16xlarge instance|Local Server(80  Intel(R) Xeon(R) CPU E7- 4870  @ 2.40GHz)|
|Benchmark Tool| benchmark server to generate YCSB-like workload|YCSB with adapter developed by ourselves based on Anna CPP Client|
|Benchmark server|AWS EC2 m4.16xlarge instance|Local Server(80  Intel(R) Xeon(R) CPU E7- 4870  @ 2.40GHz)|
|key size|8 bytes|8 bytes|
|value size|1kB|1kB|

**Testing Results**

<div>
    <img src="./icn/assets/anna-single.png" width="100%" align="center"/>
    <p align="center">Figure 1 (From the paper in 2018)</p>
</div>

<div>
    <img src="./icn/assets/anna-local-low-1.png" width="100%" align="center"/>
    <p align="center">Figure 2</p>
</div>

<div>
    <img src="./icn/assets/anna-local-high-1.png" width="100%" align="center"/>
    <p align="center">Figure 3</p>
</div>

In this experiment, we wanted to test Anna and Redis with YCSB. Since YCSB doesn't support Anna, we developed Anna's adapter based on its CPP client. And both Anna and Redis were running in the docker containers separately to guarantee they have the same physical resources. Besides, to try more configurations of Anna, we built the Jenkins pipeline to test automatically. 
Compare Figure 1 to Figure 2/3, and we have the below findings:

- Since our testing environment is different from the testing environment mentioned in the paper, we cannot reproduce the results shows in Figure 1. But by observing the comparison with Redis, when the number of threads is more than 4, Anna's performance is greater than that of Redis.
- With increasing the number of threads, the scalability can not keep. Because of our limited understanding of Anna at that time, we did not continue to explore the reason. Therefore, whether or not to test the scalability with a single node, we need further discussion.
- Under low and high contention, the number of replicas in a single node also affects the performance.
  
We also have lessons learned from this experiment:

- As this is our first experiment for Anna, we did not understand Anna's configurations well, resulting in many reworks.
- We lacked a tool or logs to check Anna's status in the experiment. 

# Distributed experiments
In these distributed experiments, we built our testing environments on AWS. And our goal is to reproduce the throughput and scalability that mentioned in the two papers.
#### Experiment 1
According to "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)" , Anna performs excellent performance and scales near-linearly as increasing the number of nodes(Figure 4 shows).

<div>
    <img src="./icn/assets/anna-distributed.png" width="100%" align="center"/>
    <p align="center">Figure 4 (From the paper in 2018)</p>
</div>

**Testing environments on AWS**

||Figure 4|Figure 5|
|:----|:----:|:----:|
|memory instance|m4.16xlarge|m4.16xlarge|
|threads number per memory node|4|4|
|routing instance|m4.large|r4.2xlarge|
|monitoring instance|m4.large|r4.2xlarge|
|key size|8 bytes|8 bytes|
|value size|1KB|1KB|

**Testing results**

<div>
    <img src="./icn/assets/anna-distributed-ycsb.png" width="100%" align="center"/>
    <p align="center">Figure 5</p>
</div>

As Anna does not provide the benchmark tool for the distributed environment, we still used YCSB client to generate requests. But with the number of memory nodes increasing,  more and more clients are required to saturate the server.  We tested two memory nodes using 64 clients and each client with 40 threads.  Figure 5 shows our testing results. 

Besides, for eight memory nodes, we have increased the clients to 80(3200 concurrencies), the throughput of Anna can not grow accordingly.  We think this behavior is not reasonable and we don't know the valid reason to explain it. 

Furthermore, compare Figure 4 with Figure 5, the throughput in Figure 5 is much lower than that in Figure 4. We also tried other configurations, but the results remain similar.

Therefore, we decided to open an [issue](https://github.com/hydro-project/cluster/issues/25) to consult the author (Chenggang Wu). Dr. Wu kindly replied to us from the following parts:

- They did not use YCSB workload generators; instead, they programmed the benchmark node to generate YCSB-like workload with Zipfian distributions and read/write ratios of their choice.
- They have added a lot more mechanisms to the system. Therefore, he expected the current throughput to be lower than in Figure 4 and suggested us to reproduce the results in Figure 6.  
- They do not update the CPP client anymore. He suggested us to import the Anna Python client to generate requests.

Since we used the code cloned from the latest repository, it is reasonable that we cannot reproduce the performance tested in earlier years. As for the scalability, the author did not reply to us directly. So Anna supposes to scale our near-linear. In order to issue this problem, we decided to jump into the next investigation to profile Anna.
 
#### Experiment 2

**Testing environment and configurations**

||Figure 6|Figure 7/8|
|:----|:----:|:----:|
|memory instance|r4.2xlarge|r4.2xlarge|
|threads number per memory node|4|4|
|routing instance|m4.large|r4.2xlarge|
|monitoring instance|m4.large|r4.2xlarge|
|key size|8 bytes|8 bytes|
|value size|256KB|256KB|
|Selective repliation|Enabled|Enabled|
|Elasticity|Disabled|Disabled|
|Cross Tire|Disabled|Disabled|
|Benchmark Tool|/|Enhanced client based on the CPP Client|

**Testing results**

<div>
    <img src="./icn/assets/anna-cluster-througput.png" width="100%" align="center"/>
    <p align="center">Figure 6 (From the paper in 2019)</p>
</div>

After the earlier experiments, we had a deeper understanding of Anna and added logs to check Anna's status, including load balance, occupancy of CPU time, etc. According to the logs, we had some findings.

Using the original CPP benchmark provided by anna, the test result under high contention(zipfian=2), with full replications shows in below table.
From the table, we can see, with more memory nodes in anna cluster. The average memory node occupancy decreases significantly. This means
every memory node is idle most of the time when there are 2 or 4 nodes. Even we add more benchmarks to try to make memory node busy, this phenomenon still remains. And also the average user request handling proportion of total occupancy decreases when more memory nodes join
the cluster.

|Memory node number|Benchmark node number|Total throughput(ops/second)|Average memory node occupancy|Average user request handling proportion of total occupancy|
|:----:|:----:|:----:|:----:|:----:|
|1 (4threads/node)|5 (6threads/node)|6295|70%|89%|
|2 (4threads/node)|6 (6threads/node)|11182|60%|76%|
|4 (4threads/node)|10 (12threads/node)|12906|30%|71%|

In order to generate enough requests to saturate the server for testing scalability, we have enhanced the CPP client based on Anna's python client to batch send requests.  Now twenty-four concurrencies can occupy 90% of a memory node's CPU time for request handling through the new client. This behavior is also consistent with the experiment mentioned in the paper. Since then, we experiment with a different number of memory nodes using the new client. 
<div>
    <img src="./icn/assets/anna-high.png" width="100%" align="center"/>
    <p align="center">Figure 7</p>
</div>

<div>
    <img src="./icn/assets/anna-low.png" width="100%" align="center"/>
    <p align="center">Figure 8</p>
</div>

From the results, Anna can keep near-linear scalability and achieve excellent throughput under both contentions, which has claimed in the papers.

# Open Questions
- Whether or not Anna can remain scalable when it extends to a large cluster? Since the maximum of memory nodes in the paper "[Autoscaling Tiered Cloud Storage in Anna](https://dsf.berkeley.edu/jmh/papers/anna_vldb_19.pdf)" is 14, we also lack the tools to test extremely large cluster.
- Why the occupancy of the memory nodes degraded to 30% even though we add more client nodes?
- How to design more real-world experiments? Since the new client sends thousands of requests one time, we think this scenario is not typical in the production system.
- Why doese the throughput of a single node not grow near-linear?

# Next Step
TBD