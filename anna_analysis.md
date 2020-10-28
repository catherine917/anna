# 1. Overview
In recent years, cloud infrastructure, which can provide the ability to scale across machines and global, has become mainstream. As HANA cannot scale out to large scales due to its architecture and cost, We indeed need a proper cloud-native NoSQL store to support various application needs. On the other hand, Anna is a low-latency, autoscaling key-value store developed in the [RISE Lab](https://rise.cs.berkeley.edu/) at [UC Berkeley](https://www.berkeley.edu/). Therefore, we want to dive deeper into Anna and verify if it is the right candidate to slot it into our "data management" domain.

To better understand Anna, we started with the two papers, "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)" and "[Autoscaling Tiered Cloud Storage in Anna](https://dsf.berkeley.edu/jmh/papers/anna_vldb_19.pdf)", published by UC Berkeley. Since we care about performance and scalability at any scale, we did several experiments to verify them. So we are going to analyze Anna's performance and scalability from the following parts:

- State model
- Cluster management

# 2. State model
According to the paper "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)", most single-server KVS systems choose the shared memory model, which results in synchronization overhead under high contention. Although these KVS systems use multi-threads to exploit multi-cores better parallelly, they will serialize when most requests perform update against the same key due to lock or atomic instructions. Therefore, this kind of design prevents the growth of the KVS system's throughput despite adding more threads and more cores. And it can't be generalized to a distributed system. But it maintains strong consistency naturally since it has only one copy of data, such as Redis.

Anna aims to provide excellent throughput at any scale, from a single multi-core machine to an extremely large cluster. So it adopts the message passing memory model instead of the shared memory model. Every thread maintains thread-local private data. The threads can only access its private state. So multi-threads can fully utilize multi-cores parallelly since no lock or atomic instructions and the majority of the CPU time processing the requests. Consequently, Anna's throughput grows near linearly when adding more threads(more cores) or more nodes even under high contention with multiple replications. But it never provides strong consistency in clusters or on a single machine(multiple-threads) when there are various replications. That means it sacrifices consistency to pursue performance and scalability.

Although Anna does not need real-time synchronization, it still needs to exchange thread-local state changes with other replicas to maintain eventual consistency between all replications. Now Anna exchanges the local dataset periodically using gossip, which is an extra cost and led to the total throughput degrade since it preempts the request handling time. So here comes the questions, whether this exchange cost is small or large in different situations? Can it be tolerated?

There are three main factors affect the size of this overhead.

- Key changesets' size of every replica.
- The number of replicas.
- Whether the multicast cross nodes.

Either changeset's size or replication numbers growth will increase the overhead. Within a single node, gossip performs between replicas(multi-threads) using the shared memory. Once across nodes, gossip will introduce network communication resulting in increasing the overhead.

Let's see gossip overhead in different situations:

- Large key changeset size within one node

  Paper "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)" has already mentioned this situation. Under low contention, the number of distinct keys being updated within the gossip period increases significantly. And the low contention part of the below figure (provided by the paper) shows that 69% of the CPU time is devoted to processing gossip when anna's replications are three, which will significantly degrade Anna's throughput. Once replications grow or cross nodes, this phenomenon will be more serious.
  <div style="text-align:center;">
    <img src="./icn/assets/cpu-time.png" />
  </div>

- Small key changeset size, within one node

  From the above figure's high contention figure, most of the CPU time(90%) is processing requests using 32 threads within one node. Moreover, the gossip overhead is small in this situation. From the figure below, in the high contention part, we can see that when replication is full, anna's throughput grows near-linear when adding more threads.
  <div style="text-align:center;">
    <img src="./icn/assets/anna-single-node-throughput.png" />
  </div>

- Small key changeset size, cross nodes

  Since the paper "[Anna: A KVS For Any Scale](https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf)" hasn't mentioned this situation. We test this on AWS by ourselves under high contention(Zipfian coefficient = 2) with full replications. We config four threads in every single node.

  From the figure below, we can see that within one node(4 threads), 90% of the CPU time is processing user requests, consistent with the paper's test results. However, when adding more nodes to two and four. Gossip overhead grows and preempts the user request handling time, degrades anna cluster's throughput.
  <div style="text-align:center;">
      <img src="./icn/assets/anna-occupancy.png" />
  </div>

According to the above discussions, gossip overhead can not avoid. When under high contention, the overhead is small within one node, so anna throughput can grow near-linear when adding more threads. But in other situations, the gossip overhead grows with more replications across nodes. And this overhead will degrade the throughput of anna. And from our perspective, in a huge cluster, the total throughput will not increase even decrease when adding more nodes if the newly added nodes will have replications of existing keys.

By the way, Anna gossips periodically despite whether Anna is busy handing the user requests. It might be the right choice if gossip is executed in the idle time, so that gossip will not preempt the request handling time. But this may bring in other problems. For examples:
- The implementation of this strategy is much more complicated.
- Each replica's data consistent will not be guaranteed if the requests continue massive.

Perhaps a combination of these two strategies about when to do gossip is better. But this is another question and is beyond the discussion of this report.

# 3. Cluster management
In the paper "[Autoscaling Tiered Cloud Storage in Anna](https://dsf.berkeley.edu/jmh/papers/anna_vldb_19.pdf)", Anna brings in three critical aspects of new designs:

- Horizontal elasticity of each tier to add and remove nodes in response to load dynamics.
- Multi-master selective replication of hot keys.
- Vertical tiering of storage layers with different cost-performance tradeoffs.

**Multi-Master Selective Replication.** Instead of maintaining the whole datasets in every replica, the selective replication of hot keys significantly reduces the changeset's size in each replica, especially under the high contention, and decreases the occupancy of gossip.  Therefore, in our first experiment, we run a high contention workload and set every node has a replication. 
<div style="text-align:center;">
    <img src="./icn/assets/anna-selective-1.png" width="100%"/>
</div>
<div style="text-align:center;">
    <img src="./icn/assets/anna-th-1.png" width="100%"/>
</div>
Compared to the previous section's result, we can see the occupancy of request handling remains similar, increasing the number of memory nodes and replicas. So the throughput grows linear. 

As for the second experiment, we run a low contention workload and maintain single replication.

<div style="text-align:center;">
    <img src="./icn/assets/anna-low-2.png" width="100%"/>
</div>
<div style="text-align:center;">
    <img src="./icn/assets/anna-th-2.png" width="100%"/>
</div>
From the result, we can see the time for gossip is less than 2% due to the cluster has only one replication. Most of the CPU time is responsible for handling requests so that the scalability is near-linear.

In our experiments, the maximum number of memory nodes is 16, and the replicas are also 16. Suppose we continue to extend the cluster to the 100 nodes and has more replicas. In that case, we are afraid that the throughput will degrade due to gossip overhead, especially under the high contention workload.
# 4. Risks
Now Anna is an experimental product. But our target is to let it become our foundation to build our key value store. So we think there is a vast gap between them. From the engineering perspective, we have some concerns about it.
<div style="text-align:center;">
    <img src="./icn/assets/anna-route.png" width="100%"  />
</div>

At first, as the figure above shows, to maintain the scalability, the client should access KVS directly. Each routing node caches the storage tiers’ hash rings and key replication vector metadata to respond to the clients’ key address requests. The client caches these addresses locally to reduce request latency and load on the routing service. They both balance the requests load. Thus the client is more complicated than the other typical client of databases. It will take more effort to enhance it or support other developing languages.

Besides, according to our investigation of [cluster management](#3-cluster-management), the selective replication of hotkeys is responsible for decreasing gossip handlers' burden. If some keys access times less than the threshold, the KVS node will degrade the key's replication to the minimum directly. The minimum is one memory replication, *k-1* disk replication(k refers to a minimum number of replicas, now the default value is 1 ).  Consequently, under high contention, most of the keys remain the minimum replication can decrease the overhead of gossip to achieve high performance. However, from the engineering perspective, suppose the production environment has only a memory layer like Redis, this configuration has a higher risk. Since the node responsible for the only copy crashed, the key's value will lose completely. 

Finally, Anna involves many complicated technologies, such as high concurrency, distributed development, database knowledge, C++ developing language, and so on, unfamiliar to us. Furthermore, developing requires us to have a good understanding of them.

# 5. Conclusion
Now we can verify the throughput mentioned in the paper. And for the small cluster(the number of memory nodes less than twenty), the scalability can maintain linear. But for the large cluster with multiple replications and under high contention workload, the scalability can not grow as linear.

Anna is an excellent experiment product, especially its actor modal,  lattice-powered, coordination-free consistency. But from the engineering perspective, it is not mature and has a long way to transform it into a product.
