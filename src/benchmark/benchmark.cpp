#include <stdlib.h>
#include <iostream>
#include "benchmark.pb.h"
#include "client/kvs_client.hpp"
#include "kvs_threads.hpp"
#include "yaml-cpp/yaml.h"

unsigned kBenchmarkThreadNum;
unsigned kRoutingThreadCount;
unsigned kDefaultLocalReplication;

// request footprints tracking parameters	
int largeFootprintSize;	
unsigned long long highLatency;	
bool track_request_footprints = false;

ZmqUtil zmq_util;
ZmqUtilInterface *kZmqUtil = &zmq_util;
double get_base(unsigned N, double skew) {
  double base = 0;
  for (unsigned k = 1; k <= N; k++) {
    base += pow(k, -1 * skew);
  }
  return (1 / base);
}

double get_zipf_prob(unsigned rank, double skew, double base) {
  return pow(rank, -1 * skew) / base;
}

void receive(KvsClientInterface *client, unsigned long *counters) {
  vector<KeyResponse> responses = client->receive_async(counters);
  while (responses.size() == 0) {
    responses = client->receive_async(counters);
  }
}

vector<KeyResponse> receive_rep(KvsClientInterface *client, unsigned long *counters, unsigned loop_counter, unsigned req_num, unsigned thread_id) {
  counters[4] = 0; //track pending get responses
  counters[5] = 0; //track pending put responses
  vector<KeyResponse> responses;
  bool next = false;
  long begin = loop_counter * req_num;
  long end = begin + req_num - 1;
  std::cout<< "begin:" << begin << std::endl;
  std::cout<< "end:" << end << std::endl;
  std::cout << next << std::endl;
  while(!next) {
    vector<KeyResponse> responses =  client->receive_rep(counters);
    for(unsigned i = 0; i < responses.size(); i++) {
       string responseId = responses[i].response_id();
       vector<string> v;
       split(responseId, '_', v);
       unsigned int tid = std::stoi(v[1]);
       long rid = std::stol(v[2]);
       std::cout << "thread id is " << tid << std::endl;
       std::cout << "rid is " << rid << std::endl;
       if ( tid == thread_id && rid >= begin && rid <= end ) {
           next = true;
           break;
       }
    }
  }
  return responses;
}

void receive_key_addr(KvsClientInterface *client, Key key, unsigned long *counters) {
  int res = client->receive_key_addr(key, counters);
  while (res == 0) {
    res = client->receive_key_addr(key, counters);
  }
}

int sample(int n, unsigned &seed, double base,
           map<unsigned, double> &sum_probs) {
  double z;           // Uniform random number (0 < z < 1)
  int zipf_value;     // Computed exponential value to be returned
  int i;              // Loop counter
  int low, high, mid; // Binary-search bounds

  // Pull a uniform random number (0 < z < 1)
  do {
    z = rand_r(&seed) / static_cast<double>(RAND_MAX);
  } while ((z == 0) || (z == 1));

  // Map z to the value
  low = 1, high = n;

  do {
    mid = floor((low + high) / 2);
    if (sum_probs[mid] >= z && sum_probs[mid - 1] < z) {
      zipf_value = mid;
      break;
    } else if (sum_probs[mid] >= z) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  } while (low <= high);

  // Assert that zipf_value is between 1 and N
  assert((zipf_value >= 1) && (zipf_value <= n));

  return zipf_value;
}

void log_request_footprints(logger log, const vector<KeyResponse> &responses) {	
  if (track_request_footprints) {	
    for (const KeyResponse& resp : responses) {	
      auto footprints_size = resp.footprints_size();	
      auto first_timestamp = resp.footprints(0).timestamp();	
      auto last_timestamp = resp.footprints(footprints_size - 1).timestamp();	
      if (last_timestamp - first_timestamp >= highLatency || footprints_size >= largeFootprintSize) {	
        log->info(parse_footprints(resp));	
      }	
    }	
  }	
}	


string generate_key(unsigned n) {
  return string(8 - std::to_string(n).length(), '0') + std::to_string(n);
}

void run(const unsigned &thread_id,
         const vector<UserRoutingThread> &routing_threads,
         const vector<MonitoringThread> &monitoring_threads,
         const Address &ip) {
  KvsClient client(routing_threads, ip, thread_id, 10000);
  string log_file = "log_" + std::to_string(thread_id) + ".txt";
  string logger_name = "benchmark_log_" + std::to_string(thread_id);
  auto log = spdlog::basic_logger_mt(logger_name, log_file, true);
  log->flush_on(spdlog::level::info);

  client.set_logger(log);
  unsigned seed = client.get_seed();

  // observed per-key avg latency
  map<Key, std::pair<double, unsigned>> observed_latency;

  // responsible for pulling benchmark commands
  zmq::context_t &context = *(client.get_context());
  SocketCache pushers(&context, ZMQ_PUSH);
  zmq::socket_t command_puller(context, ZMQ_PULL);
  command_puller.bind("tcp://*:" +
                      std::to_string(thread_id + kBenchmarkCommandPort));

  vector<zmq::pollitem_t> pollitems = {
      {static_cast<void *>(command_puller), 0, ZMQ_POLLIN, 0}};

  while (true) {
    kZmqUtil->poll(-1, &pollitems);
    if (pollitems[0].revents & ZMQ_POLLIN) {
      string msg = kZmqUtil->recv_string(&command_puller);
      log->info("Received benchmark command: {}", msg);

      vector<string> v;
      split(msg, ':', v);
      string mode = v[0];

      if (mode == "CACHE") {
        unsigned num_keys = stoi(v[1]);
        // warm up cache
        client.clear_cache();
        auto warmup_start = std::chrono::system_clock::now();

        for (unsigned i = 1; i <= num_keys; i++) {
          if (i % 50000 == 0) {
            log->info("Warming up cache for key {}.", i);
          }

          client.get_async(generate_key(i));
        }

        auto warmup_time = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now() - warmup_start)
                               .count();
        log->info("Cache warm-up took {} seconds.", warmup_time);
      } else if (mode == "LOAD") {
        string type = v[1];
        unsigned num_keys = stoi(v[2]);
        unsigned length = stoi(v[3]);
        unsigned report_period = stoi(v[4]);
        unsigned time = stoi(v[5]);
        double zipf = stod(v[6]);
        unsigned loop = stod(v[7]);
        const unsigned COUNTERS_NUM = 6;
        unsigned long counters[COUNTERS_NUM] = {0, 0, 0, 0, 0, 0};

        map<unsigned, double> sum_probs;
        double base;

        if (zipf > 0) {
          log->info("Zipf coefficient is {}.", zipf);
          base = get_base(num_keys, zipf);
          sum_probs[0] = 0;

          for (unsigned i = 1; i <= num_keys; i++) {
            sum_probs[i] = sum_probs[i - 1] + base / pow((double)i, zipf);
          }
        } else {
          log->info("Using a uniform random distribution.");
        }

        size_t count = 0;
        // auto benchmark_start = std::chrono::system_clock::now();
        // auto benchmark_end = std::chrono::system_clock::now();
        // auto epoch_start = std::chrono::system_clock::now();
        // auto epoch_end = std::chrono::system_clock::now();
        // auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
        //                       benchmark_end - benchmark_start)
        //                       .count();
        unsigned epoch = 1;

        // string keys[num_keys];
        // for(unsigned i = 0; i < num_keys; i++) {
        //   unsigned k;
        //   if(zipf > 0) {
        //     k = sample(num_keys, seed, base, sum_probs);
        //   }else {
        //     k = rand_r(&seed) % (num_keys) + 1;
        //   }
        //   Key key = generate_key(k);
        //   keys[i] =  key;
        // }
        
        log->info("Start benchmarking");
        // vector<string> keys;
        unsigned num_reqs = num_keys / loop;
        log->info("Number of requests per loop is {}", num_reqs);
        unsigned loop_counter = 0; 
        
        auto benchmark_start = std::chrono::system_clock::now();
        auto loop_start = std::chrono::system_clock::now();
        while (loop_counter < loop) {
            for(unsigned i = 0; i < num_reqs; i++) {
            // log->info("index is {}", i);
              unsigned k;
              if(zipf > 0) {
                k = sample(num_keys, seed, base, sum_probs);
              }else {
                k = rand_r(&seed) % (num_keys) + 1;
              }
              Key key = generate_key(k);
              // log->info("Key is {}", key);
              if(type == "M") {
                unsigned ts = generate_timestamp(thread_id);
                LWWPairLattice<string> val(
                    TimestampValuePair<string>(ts, string(length, 'a')));
                loop_start = std::chrono::system_clock::now();
                string req_id = client.put_async(key, serialize(val), LatticeType::LWW);
                receive_key_addr(&client, key, counters);
                client.get_async(key);
                receive_key_addr(&client, key, counters);
                counters[0] += 2;
                count += 2;
              }
              else if(type == "P") {
                unsigned ts = generate_timestamp(thread_id);
                LWWPairLattice<string> val(
                    TimestampValuePair<string>(ts, string(length, 'a')));
                string req_id = client.put_async(key, serialize(val), LatticeType::LWW);
                receive_key_addr(&client, key, counters);
                counters[0] += 1;
                count += 1;
              }
          }
          log->info("Loop {}: finish sending requests", loop_counter);
          auto responses = receive_rep(&client, counters, loop_counter, num_reqs, thread_id);
          auto loop_end = std::chrono::system_clock::now();
          log_request_footprints(log, responses);
          loop_counter++;
          auto loop_time = std::chrono::duration_cast<std::chrono::microseconds>(
                                loop_end - loop_start)
                                .count();
          log->info("[Loop {}] latency is {} microseconds.", loop_counter,
                      loop_time);

        }
        auto benchmark_end = std::chrono::system_clock::now();
        log->info("Total received responses is {}", counters[3]);
        auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
                                benchmark_end - benchmark_start)
                                .count();
        double throughput = (double)count / (double)total_time;
        log->info("[Epoch {}] Throughput is {} ops/seconds.", epoch,
                      throughput);

        log->info("Total number of request is {}, number of key_address_puller is {}, number of response_puller is {}", counters[0], counters[1],counters[2]);
        log->info("Finished");
        for (int i = 0; i < COUNTERS_NUM; i++) {
          counters[i] = 0;
        }
        UserFeedback feedback;

        feedback.set_uid(ip + ":" + std::to_string(thread_id));
        feedback.set_finish(true);

        string serialized_latency;
        feedback.SerializeToString(&serialized_latency);

        for (const MonitoringThread &thread : monitoring_threads) {
          kZmqUtil->send_string(
              serialized_latency,
              &pushers[thread.feedback_report_connect_address()]);
        }
      } else if (mode == "WARM") {
        unsigned num_keys = stoi(v[1]);
        unsigned length = stoi(v[2]);
        unsigned total_threads = stoi(v[3]);
        unsigned range = num_keys / total_threads;
        unsigned start = thread_id * range + 1;
        unsigned end = thread_id * range + 1 + range;

        Key key;
        auto warmup_start = std::chrono::system_clock::now();

        for (unsigned i = start; i < end; i++) {
          if (i % 50000 == 0) {
            log->info("Creating key {}.", i);
          }

          unsigned ts = generate_timestamp(thread_id);
          LWWPairLattice<string> val(
              TimestampValuePair<string>(ts, string(length, 'a')));

          client.put_async(generate_key(i), serialize(val), LatticeType::LWW);
          // receive(&client);
        }

        auto warmup_time = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now() - warmup_start)
                               .count();
        log->info("Warming up data took {} seconds.", warmup_time);
      } else {
        log->info("{} is an invalid mode.", mode);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 1) {
    std::cerr << "Usage: " << argv[0] << std::endl;
    return 1;
  }

  // read the YAML conf
  YAML::Node conf = YAML::LoadFile("conf/anna-config.yml");
  YAML::Node user = conf["user"];
  Address ip = user["ip"].as<string>();

  vector<MonitoringThread> monitoring_threads;
  vector<Address> routing_ips;

  YAML::Node monitoring = user["monitoring"];
  for (const YAML::Node &node : monitoring) {
    monitoring_threads.push_back(MonitoringThread(node.as<Address>()));
  }

  YAML::Node footprints = conf["footprints"];	
  if (footprints.IsDefined() && !footprints.IsNull()) {	
    track_request_footprints = true;	
    largeFootprintSize = footprints["size"].as<int>();	
    highLatency = footprints["latency"].as<unsigned long long>();	
  }

  YAML::Node threads = conf["threads"];
  kRoutingThreadCount = threads["routing"].as<int>();
  kBenchmarkThreadNum = threads["benchmark"].as<int>();
  kDefaultLocalReplication = conf["replication"]["local"].as<unsigned>();

  vector<std::thread> benchmark_threads;

  if (YAML::Node elb = user["routing-elb"]) {
    routing_ips.push_back(elb.as<string>());
  } else {
    YAML::Node routing = user["routing"];

    for (const YAML::Node &node : routing) {
      routing_ips.push_back(node.as<Address>());
    }
  }

  vector<UserRoutingThread> routing_threads;
  for (const Address &ip : routing_ips) {
    for (unsigned i = 0; i < kRoutingThreadCount; i++) {
      routing_threads.push_back(UserRoutingThread(ip, i));
    }
  }

  // NOTE: We create a new client for every single thread.
  for (unsigned thread_id = 1; thread_id < kBenchmarkThreadNum; thread_id++) {
    benchmark_threads.push_back(
        std::thread(run, thread_id, routing_threads, monitoring_threads, ip));
  }

  run(0, routing_threads, monitoring_threads, ip);
}