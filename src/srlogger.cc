#include <iostream>
#include <fstream>
#include <pthread.h>
#include "srlogger.h"

static const char *strlvls[] = {"DEBUG", "INFO", "NOTICE",
                                "WARNING", "ERROR", "CRITICAL"};


class SrLogger
{
public:
        using string = std::string;
        SrLogger(uint32_t quota = 1024, SrLogLevel lvl = SRLOG_NOTICE):
                _quota(quota), _lvl(lvl) {pthread_mutex_init(&mutex, NULL);}
        virtual ~SrLogger() {pthread_mutex_destroy(&mutex);}

        void setDest(const string &filename) {
                if (!filename.empty()) {
                        fn = filename;
                        out.open(filename, std::ios::app | std::ios::binary);
                        if (out.fail())
                                std::cerr << filename << ": Cannot open\n";
                        else
                                std::cout.rdbuf(out.rdbuf());
                }
        }
        void setQuota(uint32_t quota) {_quota = quota * 1024;}
        uint32_t getQuota() const {return _quota / 1024;}
        SrLogLevel getLevel() const {return _lvl;}
        void setLevel(SrLogLevel lvl) {_lvl = lvl;}
        bool isEnabledFor(SrLogLevel lvl) const {return _lvl <= lvl;}
        void log(SrLogLevel lvl, const string &msg);

private:
        void rotate();

private:
        std::ofstream out;
        pthread_mutex_t mutex;
        uint32_t _quota;
        string fn;
        SrLogLevel _lvl;
};


void SrLogger::log(SrLogLevel lvl, const string &msg)
{
        if (_lvl > lvl)
                return;
        char buf[30];
        const time_t now = time(NULL);
        strftime(buf, sizeof(buf), "%b %d %T ", localtime(&now));
        if (pthread_mutex_lock(&mutex) == 0) {
                std::cout << buf << strlvls[lvl] << ": " << msg << std::endl;
                if (std::cout.rdbuf() == out.rdbuf() &&
                    out.tellp() != -1 && out.tellp() > _quota)
                        rotate();
                pthread_mutex_unlock(&mutex);
        }
}


void SrLogger::rotate()
{
        rename((fn + ".1").c_str(), (fn + ".2").c_str());
        rename(fn.c_str(), (fn + ".1").c_str());
        out.close();
        out.open(fn, std::ios::trunc | std::ios::binary);
        if (out.fail())
                std::cerr << fn << ": Rotate fail.\n";
        else
                std::cout.rdbuf(out.rdbuf());
}


static SrLogger logger;

void srLogSetDest(const std::string &filename) {logger.setDest(filename);}
void srLogSetQuota(uint32_t quota) {logger.setQuota(quota);}
uint32_t srLogGetQuota() {return logger.getQuota();}
void srLogSetLevel(SrLogLevel lvl) {logger.setLevel(lvl);}
SrLogLevel srLogGetLevel() {return logger.getLevel();}
bool srLogIsEnabledFor(SrLogLevel lvl) {return logger.isEnabledFor(lvl);}

void srDebug(const std::string &msg) {logger.log(SRLOG_DEBUG, msg);}
void srInfo(const std::string &msg) {logger.log(SRLOG_INFO, msg);}
void srNotice(const std::string &msg) {logger.log(SRLOG_NOTICE, msg);}
void srWarning(const std::string &msg) {logger.log(SRLOG_WARNING, msg);}
void srError(const std::string &msg) {logger.log(SRLOG_ERROR, msg);}
void srCritical(const std::string &msg) {logger.log(SRLOG_CRITICAL, msg);}
