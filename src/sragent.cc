#include <cstdlib>
#include <signal.h>
#include <curl/curl.h>
#include <sragent.h>
#include <srlogger.h>
#include <srutils.h>
using namespace std;


static void ignoreSignal(int sig)
{
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(sig, &sa, NULL);
}


SrAgent::SrAgent(const string &_server, const string &deviceid,
                 SrIntegrate *igt, SrBootstrap *boot):
        _server(_server), did(deviceid), pboot(boot), pigt(igt)
{
        curl_global_init(CURL_GLOBAL_DEFAULT);
        ignoreSignal(SIGPIPE);
}


SrAgent::~SrAgent() {curl_global_cleanup();}


int SrAgent::bootstrap(const string &path)
{
        int c = 0;
        if (pboot) {
                c = pboot->bootstrap(path);
                _tenant = pboot->tenant();
                _username = pboot->username();
                _password = pboot->password();
        } else {
                SrBootstrap boot(_server, did);
                c = boot.bootstrap(path);
                _tenant = boot.tenant();
                _username = boot.username();
                _password = boot.password();
        }
        _auth = "Authorization: Basic ";
        _auth += b64Encode(_tenant + "/" + _username + ":" + _password);
        return c;
}


int SrAgent::integrate(const string &srv, const string &srt)
{
        int c = pigt->integrate(*this, srv, srt);
        if (c == 0) {
                xid = pigt->XID();
                id = pigt->ID();
        }
        return c;
}


int SrAgent::send(const SrNews &news) {return egress.put(news);}


static string _com(const uint32_t xid, const string &id)
{
        return id + "@" + to_string(xid);
}


void SrAgent::processMessages()
{
        SrQueue<SrOpBatch>::Event e = ingress.get(200);
        if (e.second != SrQueue<SrOpBatch>::Q_OK) return;
        const MsgXID m = strtoul(xid.c_str(), NULL, 10);
        MsgXID c = m;
        SmartRest sr(e.first.data);
        for (SrRecord r = sr.next(); r.size(); r = sr.next()) {
                MsgID j = strtoul(r[0].second.c_str(), NULL, 10);
                if (j == 87) {
                        c = strtoul(r[2].second.c_str(), NULL, 10);
                } else if (c == m) {
                        _Handler::iterator it = handlers.find(j);
                        if (it != handlers.end() && it->second) {
                                srDebug("Trigger Msg " + r[0].second);
                                (*it->second)(r, *this);
#ifdef DEBUG
                        } else {
                                srDebug("Drop Msg " + r[0].second);
#endif
                        }
                } else {
                        _XHandler::iterator it = sh.find(XMsgID(c, j));
                        if (it != sh.end() && it->second) {
                                srDebug("Trigger Msg " + _com(c, r[0].second));
                                (*it->second)(r, *this);
#ifdef DEBUG
                        } else {
                                srDebug("Drop Msg " + _com(c, r[0].second));
#endif
                        }
                }
        }
}


void SrAgent::loop()
{
        const timespec ts = {SR_AGENT_VAL/1000, (SR_AGENT_VAL%1000) * 1000000};
        while (true) {
                clock_nanosleep(CLOCK_MONOTONIC_COARSE, 0, &ts, NULL);
                for (auto &i: timers) {
                        timespec now;
                        clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
                        if (i->isActive() && i->fireTime() <= now) {
                                i->run(*this);
                                if (i->isActive()) i->start();
                        }
                }
                processMessages();
        }
}
