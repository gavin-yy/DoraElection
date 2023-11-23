#include <cstdio>
#include <string>
#include <exception>
#include <vector>
#include <memory>
#include <map>
#include <random>

#include "json.hpp"
#include "dora_election.hpp"

using namespace std;

#define LOG_PRINT(fmt, args...) printf(fmt "\n", ##args)
// #define LOG_PRINT(fmt, args...) aciga_log_debug(fmt,##args)

void enter_discovery();
void enter_election();
void enter_work();

static struct elec_adaptor *g_adaptor;

static struct gateway_info my;

enum STATE {
    S_DISCOVERY = 0,
    S_ELECTION = 1,
    S_WORKING = 2,
};

struct dev_info {
    uint32_t m_term;
    uint32_t m_weight;
    ROLE m_role;

    dev_info(uint32_t term, uint32_t weight, bool isMaster)
        : m_term(term), m_weight(weight), m_role(isMaster ? ROLE_LEADER : ROLE_FOLLOWER){};
};

struct local_info {
    string did;
    uint32_t term;
    uint32_t weight;
    ROLE r;

    enum STATE state;
};
static struct local_info my_local_info;

struct working_info {
    int discovery_tick;
    map<string, dev_info> discovery_devs;
    uint32_t cnt_dev_discovered;

    // term, votedFor_did
    map<int, string> voters;
    int election_tick;

    // record leader (in network) info.
    string leader_did;
    uint32_t leader_term;
    int heartbeat_miss;

    void *one_timer; // global timer; we only need one timer, just create this one.
    string timer_name;
    bool timer_enabled;

    working_info() : cnt_dev_discovered(0), leader_term(0), heartbeat_miss(0), one_timer(NULL) {}
};
static struct working_info my_working_info;


void save_leader_info(const string &leader, uint32_t term)
{
    my_working_info.leader_did = leader;
    my_working_info.leader_term = term;
}

void switch_rule(ROLE r)
{
    LOG_PRINT("-------- switch to role : %d --------", r);
    my_local_info.r = r;
}

#define MAX_VALUE 1000
std::random_device dev;
std::mt19937 rng(dev());
std::uniform_int_distribution<std::mt19937::result_type> distN(1, MAX_VALUE); // distribution in range [1, MAX_VALUE]

uint32_t get_random()
{
    return distN(rng);
}

// static void *discovery_timer;
// static void *election_timer;
// static void *leader_timer;
// static void *leader_check_timer;

void setup_timer(const string &timer_name, uint32_t msec, on_timeout_fn cb)
{
    if (my_working_info.one_timer) {
        g_adaptor->destory_timer(my_working_info.one_timer);
        my_working_info.one_timer = NULL;
    }

    my_working_info.timer_name = timer_name;
    my_working_info.timer_enabled = true;

    my_working_info.one_timer = g_adaptor->create_timer(NULL, msec, cb);

    assert(my_working_info.one_timer != NULL);
}

void modity_timer(uint32_t msec)
{
    assert(my_working_info.one_timer != NULL);

    g_adaptor->modify_timer(my_working_info.one_timer, msec);
}

void destory_timer()
{
    if (my_working_info.one_timer) {
        g_adaptor->destory_timer(my_working_info.one_timer);
        my_working_info.one_timer = NULL;
        my_working_info.timer_enabled = false;
        my_working_info.timer_name = {};
    }
}

static int copy_gateway_info(struct gateway_info *src, struct gateway_info *dst)
{
    *dst = *src;
    dst->did = NULL;

    if (src->did && src->did_len > 0) {
        dst->did = (char *)malloc(src->did_len + 1);
        if (!dst->did) {
            LOG_PRINT("OOM");
            return -1;
        }

        memcpy(dst->did, src->did, src->did_len);
        dst->did[src->did_len] = '\0';
        dst->did_len = src->did_len;
    }

    return 0;
}

static string build_discovery_pkt()
{
    nlohmann::json js = {
        {"method", "DiscoveryDev"},
        {"term", my_local_info.term},
        {"did", my_local_info.did},
        {"isMaster", my_local_info.r == ROLE_LEADER},
        {"checksum", 0},
    };

    return js.dump();
}

static string build_ask_vote_pkt()
{
    nlohmann::json js = {
        {"method", "AskVote"},
        {"term", my_local_info.term},
        {"did", my_local_info.did},
        {"checksum", 0},
    };

    return js.dump();
}

void handle_msg_Vote(nlohmann::json &js)
{
    LOG_PRINT("msg_vote: vote to did=%s, term=%u", string(js["did"]).c_str(), (uint32_t)(js["term"]) );

    my_working_info.voters.insert(std::pair<int, string>(js["term"], js["did"]));
}

struct vote_to {
    bool voted;
    string did;
    uint32_t term;
};
static vote_to my_vote_to;

static void vote_it(nlohmann::json &js)
{
    my_vote_to.term = js["term"];
    my_vote_to.did = js["did"];
    my_vote_to.voted = true;

    nlohmann::json vote_js = {
        {"method", "Vote"},          {"term", js["term"]}, {"did", my_local_info.did},
        {"voteFor", my_vote_to.did}, {"checksum", 0},
    };

    string pkt = vote_js.dump();

    g_adaptor->broadcast_send_pkt(pkt.c_str(), pkt.length());
}

void handle_msg_AskVote(nlohmann::json &js)
{
    if (my_local_info.r == ROLE_LEADER) {
        LOG_PRINT("we are leader, not vote to others");
        return;
    }

    // if( my_local_info.state == S_DISCOVERY ) {
    // ?
    // }
    // if( my_local_info.state == S_ELECTION ) {
    // ?
    // }
    // if( my_local_info.state == S_WORKING ) {
    //?
    // }
    LOG_PRINT("AskVote: handle it\n");

    // start to vote.
    if (js["term"] > my_vote_to.term) {
        LOG_PRINT("a newer term, update my local term ??");
        my_local_info.term = js["term"];

        return vote_it(js);
    } else if (js["term"] < my_vote_to.term) {
        LOG_PRINT("a older term, not vote");
        return;
    } else {
        if (!my_vote_to.voted) {
            return vote_it(js);
        } else {
            LOG_PRINT(" we have voted for other dev in this term [=%u], ignore this", (uint32_t)(js["term"]));
        }
    }

    return;
}

static void broadcast_devInfo()
{
    LOG_PRINT("broadcast_devInfo: role=%d", my_local_info.r);

    nlohmann::json dev_js = {{"method", "DevInfo"},
                             {"term", my_local_info.term},
                             {"weight", my_local_info.weight},
                             {"did", my_local_info.did},
                             {"isMaster", my_local_info.r == ROLE_LEADER},
                             {"checksum", 0}};

    string pkt = dev_js.dump();

    g_adaptor->broadcast_send_pkt(pkt.c_str(), pkt.length());
}

void handle_msg_DiscoveryDev(nlohmann::json &js)
{
    broadcast_devInfo();
}

static void debug_print_info()
{
    LOG_PRINT("My state:%d role:%d term:%u \nWorking leader:%s term:%u", my_local_info.state, my_local_info.r,
              my_local_info.term, my_working_info.leader_did.c_str(), my_working_info.leader_term);
}

void work_phase_recv_devInfo(nlohmann::json &js)
{
    // move out before this.
    //  if (my_local_info.r == ROLE_LEADER) {
    //      if (js["isMaster"]) {
    //          LOG_PRINT("DevInfo: find another Leader did=%s, exit leader role, restart Discovery",
    //          string(js["did"]).c_str()); my_local_info.r = ROLE_FOLLOWER; enter_discovery();
    //      }
    //      return;
    //  }

    if (my_local_info.r == ROLE_FOLLOWER) {
        if (js["isMaster"] && js["term"] == my_working_info.leader_term && js["did"] == my_working_info.leader_did) {
            LOG_PRINT("> DevInfo: follower get valid leader's heartbeat\n");
            my_working_info.heartbeat_miss = 0;
            return;
        } else {
            LOG_PRINT("> DevInfo: follower ignore this. pkt isMaster=%d did=%s term=%u\n", int(js["isMaster"]),
                      string(js["did"]).c_str(), (uint32_t)(js["term"]));
            debug_print_info();
            return;
        }
    }
}

void discovery_phase_recv_devInfo(nlohmann::json &js)
{
    // if( js["isMaster"] ) { //processed before this
    //     LOG_PRINT("DISCOVERY_PHASE: find leader did=%s term=%u", string(js["did"]).c_str(), js["term"] );

    //     save_leader_info(js["did"], js["term"]);
    //     my_working_info.discovery_devs.clear();

    //     enter_work();
    //     return;
    // } else {
    dev_info dev(js["term"], js["weight"], js["isMaster"]);
    my_working_info.discovery_devs.insert(std::pair<string, dev_info>(js["did"], dev));
    // }
}

extern "C" void on_network_recv(uint8_t *pkt, uint16_t len)
{
    LOG_PRINT("     on_network_recv IN");

    try {
        string data((char *)pkt, len);

        nlohmann::json js = nlohmann::json::parse(data);

        // 1. common process: msg'term > local_term => 1. update local_term 2. switch to follower. ;
        if (js["term"] > my_local_info.term) {
            my_local_info.term = js["term"];

            switch_rule(ROLE_FOLLOWER);
            enter_discovery(); // clear all cached info.
            return;
        }

        // 2. msg'term < local_term
        if (js["term"] < my_local_info.term && js["method"] != "DiscoveryDev") {
            LOG_PRINT("ignore msg since term(%u) < local_term(%u)", uint32_t(js["term"]), my_local_info.term);
            return;
        }

        // 3. DiscoveryDev.
        if (js["method"] == "DiscoveryDev") {
            handle_msg_DiscoveryDev(js);
            return;
        }

        // 4. other device announce it is leader.
        if (js["method"] == "DevInfo" && js["isMaster"] ) {
            LOG_PRINT("rx Leader announce did=%s term=%u", string(js["did"]).c_str(), (uint32_t)(js["term"]) );

            if (my_local_info.r == ROLE_LEADER) {
                LOG_PRINT("Find another leader did=%s term=%u, we give up leadership",
                        string(js["did"]).c_str(), (uint32_t)(js["term"]) );
                enter_discovery(); // check it again...
                return;
            }

            if( my_local_info.state == S_DISCOVERY || my_local_info.state == S_ELECTION ) {
                LOG_PRINT("Find leader did=%s term=%u", string(js["did"]).c_str(), (uint32_t)(js["term"]) );

                save_leader_info(js["did"], js["term"]);
                my_working_info.discovery_devs.clear();

                enter_work();
                return;
            }

            //work_mode + follower : check heartbeat bellow.
        }

        if (js["method"] == "DevInfo") {
            if (my_local_info.state == S_DISCOVERY) {
                discovery_phase_recv_devInfo(js);
            } else if (my_local_info.state == S_WORKING) {
                work_phase_recv_devInfo(js);
            } else {
                // election phase
                LOG_PRINT("ignore DevInfo msg in this phase [=%d]", my_local_info.state);
            }
            return;
        }

        if (js["method"] == "Vote") {
            // 1. we are leader (means in work mode), not vote to other device.
            // 2. we are in election mode (voted to myself)
            if (my_local_info.r == ROLE_LEADER || my_local_info.state == S_ELECTION) {
                LOG_PRINT("ignore Vote msg in this phase [=%d], role=%d", my_local_info.state, my_local_info.r);
                return;
            }

            handle_msg_Vote(js);
            return;
        }

        if (js["method"] == "AskVote") {
            handle_msg_AskVote(js);
            return;
        }

    } catch (std::exception &e) {
        LOG_PRINT("rx json error: %s\n", e.what());
    }
}





extern "C" void on_discovery_timeout(void *timer, void *user_arg)
{
    // 1. send discovery packet periodly, so other machine can reply DevInfo to us.
    ++my_working_info.discovery_tick;
    if (my_working_info.discovery_tick < 3) {
        string discovery_pkt = build_discovery_pkt();
        g_adaptor->broadcast_send_pkt(discovery_pkt.c_str(), discovery_pkt.size());
        modity_timer(1000 * 10);
        return;
    } else {
        // discovery timeout, chcck collected info here..
        my_working_info.discovery_tick = 0;
    }

    // 2.discovery timeout.

    if (my_working_info.discovery_devs.size() == 0) {
        LOG_PRINT("DISCOVERY_PHASE: No other gateway was found, I will be Leader\n");
        my_local_info.term += 1;  //new term here.
        switch_rule(ROLE_LEADER);
        enter_work();
        return;
    }

    // 3. check max wight
    uint32_t max_weight = 0;
    for (auto it = my_working_info.discovery_devs.begin(); it != my_working_info.discovery_devs.end(); ++it) {
        if (it->second.m_weight > max_weight) {
            max_weight = it->second.m_weight;
        }
    }
    my_working_info.cnt_dev_discovered = my_working_info.discovery_devs.size();

    if (my_local_info.weight >= max_weight) {
        LOG_PRINT("DISCOVERY_PHASE: enter election phase");
        enter_election();
    } else {
        LOG_PRINT("DISCOVERY_PHASE: we are not the max wight, keep discovery");
        enter_discovery();
    }
}

extern "C" void on_election_timeout(void *timer, void *user_arg)
{
    // 1. send AskVote periodly..
    ++my_working_info.election_tick;
    if (my_working_info.election_tick < 5) {
        LOG_PRINT("Election Phase: send askVote msg\n");
        string ask_vote = build_ask_vote_pkt();
        g_adaptor->broadcast_send_pkt(ask_vote.c_str(), ask_vote.size());
    }

    // 2. election timeout
    size_t cnt = my_working_info.voters.size();

    if (cnt + 1 >= (my_working_info.cnt_dev_discovered + 1) / 2) {
        LOG_PRINT("ELECTION_RESULT: get enough votes");
        my_local_info.r = ROLE_LEADER;

        enter_work();
    } else {
        LOG_PRINT("ELECTION_RESULT: not get enough votes. cnt=%zu devs_cnt=%u", cnt,
                  my_working_info.cnt_dev_discovered);
        enter_discovery();
    }
}

extern "C" void on_leader_broadcast_timeout(void *timer, void *user_arg)
{
    if (my_local_info.r == ROLE_LEADER) {
        broadcast_devInfo();
        // g_adaptor->modify_timer(leader_timer, 1000 * 10);
        modity_timer(1000 * 10);
    } else {
        LOG_PRINT("we are not leader now, not broadcast leader's DiscoveryDev msg");
    }
}

void on_leader_alive_check(void *timer, void *user_arg)
{
    if (my_working_info.heartbeat_miss >= 3) {
        LOG_PRINT("!!! Leader heartbeat lost, restart new election");

        enter_discovery();
    } else {
        LOG_PRINT("!!! Leader heartbeat check ok, miss=%d", my_working_info.heartbeat_miss);
        ++my_working_info.heartbeat_miss;
        modity_timer(1000 * 10);
        // g_adaptor->modify_timer(leader_check_timer, 1000 * 10);
    }
}

static void clear_working_info()
{
    my_working_info.discovery_tick = 0;
    my_working_info.discovery_devs.clear();

    //if we voted to one device, should remember it.
    // my_working_info.voters.clear();
    auto it = my_working_info.voters.begin();
    while(it != my_working_info.voters.end())
    {
        if( it->first < my_local_info.term ) {
            it = my_working_info.voters.erase(it);
        } else {
            ++it;
        }
    }

    my_working_info.leader_did = {};
    my_working_info.leader_term = 0;
    my_working_info.heartbeat_miss = 0;
}


void enter_discovery()
{
    LOG_PRINT("Enter enter_discovery\n");
    my_local_info.state = S_DISCOVERY;

    clear_working_info();
    destory_timer();

    assert(my_local_info.r == ROLE_FOLLOWER);

    string discovery_pkt = build_discovery_pkt();

    g_adaptor->broadcast_send_pkt(discovery_pkt.c_str(), discovery_pkt.size());

    setup_timer("discovery", 1000 * 10, on_discovery_timeout);
}

void enter_election()
{
    LOG_PRINT("Enter enter_election\n");
    my_local_info.state = S_ELECTION;
    my_working_info.election_tick = 0;

    // start election.
    my_local_info.term += 1;

    LOG_PRINT("Election Phase: send askVote msg\n");
    string ask_vote = build_ask_vote_pkt();
    g_adaptor->broadcast_send_pkt(ask_vote.c_str(), ask_vote.size());

    setup_timer("election", 1000 * 2 + (get_random() % 100), on_election_timeout);

    return;
}

void enter_work()
{
    LOG_PRINT("Enter enter_work\n");
    my_local_info.state = S_WORKING;

    if (my_local_info.r == ROLE_LEADER) {
        LOG_PRINT("we are LEADER, start leader timer");
        broadcast_devInfo();

        setup_timer("leader_hb", 1000 * 10, on_leader_broadcast_timeout);

        return;
    }

    // we are follower. check whether leader is alive.
    LOG_PRINT("we are FOLLOWER, start leader alive check timer");
    setup_timer("leader_hb_check", 1000 * 10, on_leader_alive_check);
}

int elec_start()
{
    enter_discovery();

    return 0;
}

ROLE elec_get_current_role()
{
    return my_local_info.r;
}

static uint32_t init_weight(DEV_TYPE dev_type, uint32_t data_ver)
{
    uint32_t weight = 0;

    switch (dev_type) {
    case GW_P5:
        weight = 100 * 10000;
        break;
    case GW_P3:
        weight = 90 * 10000;
        break;
    case GW_P3L:
        weight = 80 * 10000;
        break;
    case GW_P2:
        weight = 70 * 10000;
        break;
    default:
        break;
    }

    weight += data_ver;
    return weight;
}

int elec_init(struct elec_adaptor *adaptor, struct gateway_info *info)
{
    if (!adaptor) {
        return -1;
    }

    g_adaptor = adaptor;

    int ret = copy_gateway_info(info, &my);
    if (ret != 0) {
        return -1;
    }
    my_local_info.did = string(my.did);
    my_local_info.weight = init_weight(my.dev_type, my.data_ver);

    g_adaptor->register_recv_cb(on_network_recv);

    LOG_PRINT("++ elec_init success, my.did=%s my.weight=%u\n", my.did, my_local_info.weight);

    return 0;
}
