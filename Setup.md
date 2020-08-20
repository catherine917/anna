## Anna Cluster Set up on AWS (Local Mode)

### Set up instance
- Login AWS Console
- Choose EC2 Service
- Click AMI link(on the left side of the console)
- Select `anna-image-last` image and start
- Instance type: `r4.2xlarge`
- Security group: `launch-wizard-12`
- Private key: `hydro.k8s.local.pem`

### Code update and build
```
cd anna 
git pull origin master
cd common
git pull origin master
cd ..
cd build
make -j
```

### Monitor Configuration
- ssh connect to the monitor node
```
cd anna
./scripts/start-anna.sh mn
vim ./conf/anna-config.yml // modify config file and save
./build/target/kvs/anna-monitor
```

### Routing Configuration
- ssh connect to the routing node 
```
cd anna
./scripts/start-anna.sh r
vim ./conf/anna-config.yml // modify config file and save
./build/target/kvs/anna-route
```

### KVS Configuration
- ssh connect to the KVS node
```
cd anna
./scripts/start-anna.sh 
vim ./conf/anna-config.yml
./build/target/kvs/anna-kvs
```
- kvs config file sample
  
```yml
server:
  monitoring: [] //IP addresses of monitoring nodes
  routing: [] // IP addresses of routing nodes
  seed_ip:  // IP addresses of routing nodes
  public_ip: 127.0.0.1
  private_ip: 127.0.0.1
  mgmt_ip: "NULL"

```

## Benchmark Configuration
- ssh connect to the benchmark node
```
cd anna
./scripts/start-anna.sh b
vim ./conf/anna-config.yml
./build/target/benchmark/anna-bench
```
- benchmark config file sample
```yml
user: # for client user related node, e.g., cli
  monitoring: []  # IP addresses of monitoring nodes
  routing: []    # IP addresses of routing nodes
  ip: 127.0.0.1 # IP address of this node
```

## Benchmark Trigger Configuration
- ssh connect to the trigger node
```
cd anna
vim ./conf/anna-config.yml
./build/target/benchmark/anna-bench-trigger 12 (same as the configuration of benchmark threads)
```
- benchmark trigger config file sample
```yml
benchmark:
     - 127.0.0.1 // IP address of the benchmark nodes
     - 127.0.0.2 
```
- benchmark commonds
```
LOAD:M:100000:256000:20:480:2 

LOAD:M(put at first then get)/P(PUT)/G(GET):100000(number of keys):256000(length of the value):20(report period):480(running time):2(zipfian factor)
```