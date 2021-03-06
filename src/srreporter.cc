#include <algorithm>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include "srreporter.h"
#define Q_OK SrQueue<SrNews>::Q_OK
using namespace std;

#define SR_FILEBUF_VER 0x1
#define SR_FILEBUF_PAGE_BASE 9
#define SR_FILEBUF_PAGE_SIZE (1<<(SR_FILEBUF_PAGE_BASE+SR_FILEBUF_PAGE_SCALE))
#define SR_FILEBUF_INDEX_SUFFIX ".index"
#define SR_MEMBUF_SCALE 8
#define SR_MEMBUF_NUM (1 << SR_MEMBUF_SCALE)
#define BASE_PAGE(x) (x & 0x07)
#define BASE_VER(x) ((x >> 3) & 0x0f)
#define _BASE (BASE_PAGE(SR_FILEBUF_PAGE_SCALE) | (SR_FILEBUF_VER<<3))


struct _BFHead {
        _BFHead(): base(_BASE), flag(0), size(0), cnt(0), pad2(0) {}
        uint8_t base, flag;
        uint16_t size, cnt, pad2;
};


struct _BFPage {
        _BFPage(uint16_t idx = 0, uint16_t oft = 0, uint8_t f = 0):
                index(idx), offset(oft), flag(f), cnt(0), pad(0) {}
        uint16_t index, offset;
        uint8_t flag, cnt;
        uint16_t pad;
};

typedef std::deque<_BFPage> _PCB;
typedef std::vector<bool> _UFLAG;

static void readPCB(const string &fn, _BFHead &head, _PCB &pcb, _UFLAG &uflag)
{
        ifstream fs(fn, ios::binary);
        if (!fs.read((char*)&head, sizeof(head)))
                return;
        if (BASE_VER(head.base) != SR_FILEBUF_VER ||
            BASE_PAGE(head.base) != SR_FILEBUF_PAGE_SCALE) {
                head.cnt = head.size = 0;
                return;
        }
        const size_t sz = head.size;
        if (uflag.size() < sz)
                uflag.resize(sz);
        _BFPage page;
        uint8_t flag = 2;
        uint16_t cnt = 0;
        for (size_t i = 0; fs.read((char*)&page, sizeof(page)) && i < sz; ++i) {
                pcb.push_back(page);
                uflag[page.index] = true;
                cnt += flag == page.flag ? 0 : 1;
                flag = page.flag;
        }
        head.size = pcb.size();
        head.cnt = cnt;
}


static void writePCB(const string &fn, const _BFHead &head, _PCB &pcb)
{
        ofstream out(fn, ios::binary);
        if (!out.write((const char*)&head, sizeof(head)))
                return;
        for (const auto &e: pcb) {
                if (!out.write((const char*)&e, sizeof(_BFPage)))
                        break;
        }
}


static int readPage(ifstream &in, size_t i, char *dest, size_t size)
{
        in.seekg(i * SR_FILEBUF_PAGE_SIZE);
        return in.read(dest, size) ? in.gcount() : -1;
}


static int writePage(ofstream &out, size_t index, const char *buf,
                     size_t size, size_t offset = 0)
{
        out.seekp(index * SR_FILEBUF_PAGE_SIZE + offset);
        return out.write(buf, size) ? 0 : -1;
}


class _Pager
{
public:
        _Pager(uint16_t _cap): cap(_cap) {}
        virtual ~_Pager() {}

        size_t capacity() const {return cap;};
        void setCapacity(uint16_t _cap) {cap = _cap;};
        virtual bool empty() const = 0;
        virtual size_t bsize() const = 0;
        virtual size_t size() const = 0;
        virtual string front() const = 0;
        virtual void pop_front() = 0;
        virtual int emplace_back(const string &s) = 0;
        virtual void clear() = 0;

protected:
        uint16_t cap;
};


class _BFPager: public _Pager {
public:
        _BFPager(const string &_fn, int c): _Pager(c), fn(_fn), uflag(c) {
                readPCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb, uflag);
                if (access(fn.c_str(), F_OK) == -1) ofstream out(fn);
        }
        ~_BFPager() {writePCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb);}

        virtual bool empty() const {return head.size == 0;}
        virtual size_t bsize() const {return head.cnt;}
        virtual size_t size() const {return head.size;}
        virtual string front() const {
                string s;
                if (pcb.empty())
                        return s;
                char buf[SR_FILEBUF_PAGE_SIZE];
                ifstream in(fn, ios::binary);
                const auto flag = pcb.front().flag;
                for (size_t i = 0; i < pcb.size() && flag == pcb[i].flag; ++i) {
                        const auto offset = pcb[i].offset + 1;
                        if (readPage(in, pcb[i].index, buf, offset) != offset)
                                break;
                        s.append(buf, offset);
                }
                return s;
        };
        virtual void pop_front() {
                const auto flag = pcb.front().flag;
                size_t i = 0;
                for (; i < pcb.size() && pcb[i].flag == flag; ++i)
                        uflag[pcb[i].index] = false;
                pcb.erase(pcb.begin(), pcb.begin() + i);
                head.size = pcb.size();
                --head.cnt;
                writePCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb);
        }
        virtual int emplace_back(const string &s) {
                const auto sz = SR_FILEBUF_PAGE_SIZE;
                if (pcb.empty() || s.size() + pcb.back().offset > sz) {
                        return push_back(s);
                } else {
                        auto &t = pcb.back();
                        const auto f = t.offset + 1;
                        ofstream out(fn, ios::binary | ios::in);
                        if (writePage(out, t.index, s.c_str(), s.size(), f))
                                return -1;
                        t.offset += s.size();
                        writePCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb);
                        return 0;
                }
        }
        virtual void clear() {
                pcb.clear();
                fill(uflag.begin(), uflag.end(), false);
                head.size = head.cnt = 0;
                writePCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb);
                const auto c = cap;
                if (c < uflag.size()) {
                        truncate(fn.c_str(), c * SR_FILEBUF_PAGE_SIZE);
                        uflag.resize(c);
                        srInfo("filebuf: truncate " + to_string(c));
                }
        }

private:
        virtual int push_back(const string &s) {
                const auto _cap = cap;
                if (uflag.size() < _cap) uflag.resize(_cap);

                const uint8_t flag = (pcb.empty() || pcb.back().flag) ? 0 : 1;
                const auto sz = SR_FILEBUF_PAGE_SIZE;
                const int N = s.size() / sz;
                ofstream out(fn, ios::binary | ios::in);
                const char *buf = s.c_str();
                for (int i = 0; i < N; ++i) {
                        const auto index = get_free_page();
                        if (writePage(out, index, buf + i * sz, sz) == -1)
                                return -1;
                        uflag[index] = true;
                        pcb.emplace_back(index, sz - 1, flag);
                }
                const auto c = s.size() & (sz - 1);
                if (c) {
                        const auto index = get_free_page();
                        if (writePage(out, index, buf + N * sz, c) == 0) {
                                uflag[index] = true;
                                pcb.emplace_back(index, c - 1, flag);
                        }
                }
                head.size = pcb.size();
                ++head.cnt;
                writePCB(fn + SR_FILEBUF_INDEX_SUFFIX, head, pcb);
                return 0;
        }
        uint16_t get_free_page() {
                uint16_t index = 0;
                for (; index < cap && uflag[index]; ++index);
                if (index >= cap) {
                        index = pcb.front().index;
                        pop_front();
                }
                return index;
        }

        std::deque<_BFPage> pcb;
        std::string fn;
        _BFHead head;
        std::vector<bool> uflag;
};


class _MemPager: public _Pager
{
public:
        _MemPager(uint16_t _cap): _Pager(_cap) {}
        virtual ~_MemPager() {}

        virtual bool empty() const {return mcb.empty();}
        virtual size_t bsize() const {
                const auto s = mcb.size();
                const auto b = s & (SR_MEMBUF_NUM - 1);
                return (s >> SR_MEMBUF_SCALE) + (b ? 1 : 0);
        }
        virtual size_t size() const {return mcb.size();}
        virtual string front() const {
                string s;
                for (size_t i = 0; i < mcb.size() && i < SR_MEMBUF_NUM; ++i)
                        s += mcb[i];
                return s;
        }
        virtual void pop_front() {
                auto p = [](const string &T) {return !T.compare(0, 3, "15,");};
                if (mcb.size() <= SR_MEMBUF_NUM) {
                        mcb.clear();
                } else if (p(mcb[SR_MEMBUF_NUM])) {
                        mcb.erase(mcb.begin(), mcb.begin() + SR_MEMBUF_NUM);
                } else {
                        const auto a = mcb.size() - SR_MEMBUF_NUM;
                        auto it = find_if(mcb.rbegin() + a, mcb.rend(), p);
                        const auto s = *it;
                        mcb.erase(mcb.begin(), mcb.begin() + SR_MEMBUF_NUM);
                        mcb.push_front(s);
                }
        }
        virtual int emplace_back(const string &s) {
                auto p = [](const string &T) {return T.compare(0, 3, "15,");};
                if (mcb.size() >= cap) {
                        if (p(mcb.front())) {
                                mcb.pop_front();
                        } else {
                                const string front(mcb.front());
                                mcb.erase(mcb.begin(), mcb.begin() + 2);
                                if (!mcb.empty() && p(mcb.front()))
                                        mcb.emplace_front(front);
                        }
                }
                mcb.emplace_back(s);
                return 0;
        }
        virtual void clear() {mcb.clear();}

private:
        deque<string> mcb;
};


SrReporter::SrReporter(const string &s, const string &x, const string &a,
                       SrQueue<SrNews> &out, SrQueue<SrOpBatch> &in,
                       uint16_t cap, const string fn):
        http(new SrNetHttp(s + "/s", "", a)), mqtt(), out(out), in(in), xid(x),
        ptr(), sleeping(false), isfilebuf(!fn.empty())
{
        if (isfilebuf)
                ptr.reset(new _BFPager(fn, cap));
        else
                ptr.reset(new _MemPager(cap));
}


SrReporter::SrReporter(const string &server, const string &deviceId,
                       const string &x, const string &user, const string &pass,
                       SrQueue<SrNews> &out, SrQueue<SrOpBatch> &in,
                       uint16_t cap, const string fn):
        http(), mqtt(new SrNetMqtt("d:" + deviceId, server)), out(out), in(in),
        xid(x), ptr(), sleeping(false), isfilebuf(!fn.empty())
{
        if (isfilebuf)
                ptr.reset(new _BFPager(fn, cap));
        else
                ptr.reset(new _MemPager(cap));
        mqtt->setUsername(user.c_str());
        mqtt->setPassword(pass.c_str());
}


SrReporter::~SrReporter() {}
uint16_t SrReporter::capacity() const {return ptr->capacity();}
void SrReporter::setCapacity(uint16_t cap) {ptr->setCapacity(cap);}


int SrReporter::start()
{
        pthread_t tid;
        int no = pthread_create(&tid, NULL, func, this);
        if (no)
                srError("reporter: start failed, " + string(strerror(no)));
        return no;
}


void SrReporter::mqttSetOpt(int opt, long parameter)
{
        switch (opt) {
        case SR_MQTTOPT_KEEPALIVE: mqtt->setKeepalive(parameter); break;
        default: srWarning("reporter: invalid mqtt option " + to_string(opt));
        }
}


static string aggregate(SrQueue<SrNews> &q, _Pager *p, bool isfilebuf,
                        const string &xid)
{
        string s, buf, myxid;
        SrQueue<SrNews>::Event e;
        while ((e = q.get(SR_REPORTER_VAL)).second == Q_OK) {
                const string &data = e.first.data;
                const bool alternate = e.first.prio & SR_PRIO_XID;
                const size_t pos = alternate ? data.find(',') : 0;
                const string cxid = alternate ? data.substr(0, pos) : xid;
                if (cxid != myxid) { // different XID than before
                        myxid = cxid;
                        s += "15," + myxid + '\n';
                        if (e.first.prio & SR_PRIO_BUF) {
                                if (isfilebuf)
                                        buf += "15," + myxid + '\n';
                                else
                                        p->emplace_back("15," + myxid + '\n');
                        }
                }
                const size_t pos2 = pos ? pos + 1 : 0;
                s.append(data, pos2, data.size() - pos2);
                s += '\n';
                if (e.first.prio & SR_PRIO_BUF) {
                        if (isfilebuf) {
                                buf.append(data, pos2, data.size() - pos2);
                                buf += '\n';
                        } else {
                                p->emplace_back(data.substr(pos2) + '\n');
                        }
                }
        }
        if (!buf.empty()) p->emplace_back(buf);
        return s;
}


class MyMqttMsgHandler: public SrMqttAppMsgHandler
{
public:
        MyMqttMsgHandler(SrQueue<SrOpBatch>& _in): in(_in) {}
        virtual void operator()(const SrMqttAppMsg &m) {
                in.put(SrOpBatch(m.data));
        }
private:
        SrQueue<SrOpBatch> &in;
};


class MyMqttDebugHandler: public SrMqttAppMsgHandler
{
public:
        virtual void operator()(const SrMqttAppMsg &m) {
                srWarning("MQTT err: " + m.topic + ", " + m.data);
        }
};


static int _mqtt_connect(SrNetMqtt *mqtt, bool clean, const string &xid)
{
        const string topics[] = {"s/dl", "s/ol/" + xid, "s/e"};
        int qos[] = {1, 1, 0}, N = 3;
        if (mqtt->connect(clean) == -1)
                return -1;
        return mqtt->subscribe(topics, qos, N);
}


static int exp_send(void *net, bool ishttp, const string &data,
                    SrQueue<SrOpBatch> &in, const string &xid)
{
        SrNetHttp *http = ishttp ? (SrNetHttp*)net : nullptr;
        SrNetMqtt *mqtt = !ishttp ? (SrNetMqtt*)net : nullptr;
        int i;
        for (i = 0; i < SR_REPORTER_RETRIES; ++i) {
                if (http && http->post(data) >= 0) {
                        const string &resp = http->response();
                        if (!resp.empty()) {
                                in.put(SrOpBatch(resp));
                                http->clear();
                        }
                        break;
                }
                if (mqtt) {
                        if(mqtt->publish("s/ul", data, 2))
                                _mqtt_connect(mqtt, false, xid);
                        else
                                break;
                }
                ::sleep(1 << i);
        }
        return i < SR_REPORTER_RETRIES ? 0 : -1;
}


void *SrReporter::func(void *arg)
{
        SrReporter *rpt = (SrReporter*)arg;
        int rc = 0;
        void *net = rpt->http ? (void*)rpt->http.get() : (void*)rpt->mqtt.get();
        _Pager *pager = rpt->ptr.get();
        MyMqttMsgHandler mh(rpt->in);
        MyMqttDebugHandler mherr;
        bool ishttp = rpt->http != nullptr;
        if (rpt->http) {
                rpt->http->setTimeout(30);
        } else if (rpt->mqtt) {
                rpt->mqtt->setTimeout(15);
                rpt->mqtt->addMsgHandler("s/dl", &mh);
                rpt->mqtt->addMsgHandler("s/ol/" + rpt->xid, &mh);
                rpt->mqtt->addMsgHandler("s/e", &mherr);
                _mqtt_connect(rpt->mqtt.get(), false, rpt->xid);
        }
        if (rpt->isfilebuf) {
                const string s = "filebuf: " + to_string(SR_FILEBUF_VER);
                srNotice(s + ", " + to_string(SR_FILEBUF_PAGE_SIZE));
        }
        srInfo("reporter: buf capacity: " + to_string(pager->capacity()));
        size_t bsize = pager->bsize();
        string data = pager->front();
        string aggre = aggregate(rpt->out, pager, rpt->isfilebuf, rpt->xid);
        if (bsize <= 1) data += aggre;
        if (!data.empty()) {
                rc = exp_send(net, ishttp, data, rpt->in, rpt->xid);
                if (rc == 0) {
                        if (bsize <= 1) pager->clear();
                        else pager->pop_front();
                }
        }
        srInfo("reporter: listening...");
        while (true) {
                // pre-fetching
                bsize = pager->bsize();
                data = pager->front();
                if (rpt->mqtt && rpt->mqtt->yield(1000) == -1)
                        _mqtt_connect(rpt->mqtt.get(), false, rpt->xid);
                aggre = aggregate(rpt->out, pager, rpt->isfilebuf, rpt->xid);
                if (bsize <= 1) data += aggre;
                // sleeping mode
                if (rpt->sleeping || data.empty()) continue;
                // exponential wait
                rc = exp_send(net, ishttp, data, rpt->in, rpt->xid);
                if (rc == 0) {
                        if (bsize <= 1) pager->clear();
                        else pager->pop_front();
                }
        }
        return NULL;
}
