// OWNERSHIP=Claude
// Password login for the GUI on the internet (#109): one shared password
// (GUI_PASSWORD) and a signed cookie, so the server can sit behind a reverse
// proxy. Nothing here touches a socket; gui_server.cpp asks it questions.
//
// The cookie holds an expiry and an HMAC-SHA256 over it. Its key is derived
// from GUI_COOKIE_SECRET and the password together, so a restart keeps every
// login (the secret comes from the environment, not from this run) and a new
// password or a new secret logs everyone out. Nothing is stored per login:
// logging out removes the cookie from that browser, and a copied cookie stays
// good until it expires or the secret or password changes.
//
// Failed logins are counted per client address (an IPv6 address by its /64,
// #111) and over the whole server within a sliding window; past either limit
// a login is refused before the password is even looked at, so guessing
// cannot go on at full speed.
#ifndef GUI_ACCESS_HPP
#define GUI_ACCESS_HPP
#include "sha256.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <deque>
#include <map>
#include <string>

namespace gui_access
{
    constexpr long long COOKIE_SECONDS = 30LL*24*3600;
    constexpr long long LOGIN_WINDOW_MS = 5*60*1000;
    constexpr int DEFAULT_FAILS_PER_IP = 10;
    constexpr int DEFAULT_FAILS_TOTAL = 100;
    constexpr size_t MIN_SECRET_CHARS = 16;
    constexpr const char* COOKIE_NAME = "gui_login";

    // The value of cookie `name` in a Cookie header's "a=1; b=2" list, or "".
    inline std::string cookie_value(const std::string& header, const std::string& name)
    {
        for(size_t i=0;i<header.size();)
        {
            while(i<header.size() && header[i]==' ')
            i++;
            size_t semi = header.find(';', i);
            size_t end = semi==std::string::npos ? header.size() : semi;
            size_t eq = header.find('=', i);
            if(eq!=std::string::npos && eq<end && eq-i==name.size() && header.compare(i, eq-i, name)==0)
            return header.substr(eq+1, end-eq-1);
            if(semi==std::string::npos)
            break;
            i = semi+1;
        }
        return "";
    }

    class Login
    {
        public:
        bool on() const { return enabled; }

        // False with `error` set for a secret too short to sign with.
        bool setup(const std::string& password, const std::string& secret, std::string& error)
        {
            if(password.empty())
            {
                error = "GUI_PASSWORD is empty";
                return false;
            }
            if(secret.size()<MIN_SECRET_CHARS)
            {
                error = "GUI_COOKIE_SECRET must be set, at least " + std::to_string(MIN_SECRET_CHARS)
                      + " characters (e.g. the output of: openssl rand -hex 32), or every restart would log everyone out";
                return false;
            }
            cookie_key = sha256::hmac(secret, "cookie\n" + password);
            check_key = sha256::hmac(secret, "password");
            password_mac = sha256::hmac(check_key, password);
            enabled = true;
            return true;
        }

        // Both sides go through the HMAC first, so the comparison takes the
        // same time whatever was typed, its length included.
        bool password_ok(const std::string& typed) const
        {
            return enabled && sha256::equal(sha256::hmac(check_key, typed), password_mac);
        }

        // "<expiry>.<mac>", good until now_s + COOKIE_SECONDS.
        std::string make_cookie(long long now_s) const
        {
            std::string expiry = std::to_string(now_s+COOKIE_SECONDS);
            return expiry + "." + sha256::hex(sha256::hmac(cookie_key, "v1." + expiry));
        }

        bool cookie_ok(const std::string& value, long long now_s) const
        {
            if(!enabled)
            return false;
            size_t dot = value.find('.');
            if(dot==std::string::npos || dot==0 || dot>18)
            return false;
            std::string expiry = value.substr(0, dot);
            for(char c : expiry)
            if(c<'0' || c>'9')
            return false;
            std::string mac = sha256::hex(sha256::hmac(cookie_key, "v1." + expiry));
            if(!sha256::equal(value.substr(dot+1), mac))
            return false;
            return std::stoll(expiry)>now_s;
        }

        private:
        bool enabled = false;
        std::string cookie_key, check_key, password_mac;
    };

    // Failed logins in the last LOGIN_WINDOW_MS, per address and in total.
    class Login_Limiter
    {
        public:
        int per_ip = DEFAULT_FAILS_PER_IP;
        int total = DEFAULT_FAILS_TOTAL;

        // 0 when `ip` may try now, else how long until it may (ms).
        // `whole_server` says which limit it is.
        long long wait_ms(const std::string& ip, long long now_ms, bool& whole_server)
        {
            prune(now_ms);
            whole_server = false;
            auto it = by_ip.find(group_key(ip));
            if(it!=by_ip.end() && (int)it->second.size()>=per_ip)
            return it->second.front()+LOGIN_WINDOW_MS-now_ms;
            if((int)all.size()>=total)
            {
                whole_server = true;
                return all.front()+LOGIN_WINDOW_MS-now_ms;
            }
            return 0;
        }

        void failed(const std::string& ip, long long now_ms)
        {
            by_ip[group_key(ip)].push_back(now_ms);
            all.push_back(now_ms);
        }

        // Every remembered group (an address, or an IPv6 /64) had a failure
        // inside the window, and the total limit refuses more than `total` of
        // those, so the table never holds more than `total` groups.
        size_t addresses() const { return by_ip.size(); }

        private:
        std::map<std::string, std::deque<long long>> by_ip;
        std::deque<long long> all;

        // One IPv6 home connection can draw on billions of addresses, enough
        // to keep `total` used up by itself and lock out every other visitor
        // without ever repeating an address (#111) — so every address in the
        // same /64 (what a residential ISP routes to one connection) shares
        // one bucket. An IPv4 or IPv4-mapped address is its own bucket, since
        // nothing is handed out on that scale over IPv4.
        static std::string group_key(const std::string& ip)
        {
            in6_addr a;
            if(inet_pton(AF_INET6, ip.c_str(), &a)!=1 || IN6_IS_ADDR_V4MAPPED(&a))
            return ip;
            return std::string(reinterpret_cast<const char*>(&a), 8);
        }

        void prune(long long now_ms)
        {
            while(!all.empty() && now_ms-all.front()>=LOGIN_WINDOW_MS)
            all.pop_front();
            for(auto it=by_ip.begin();it!=by_ip.end();)
            {
                while(!it->second.empty() && now_ms-it->second.front()>=LOGIN_WINDOW_MS)
                it->second.pop_front();
                if(it->second.empty())
                it = by_ip.erase(it);
                else
                ++it;
            }
        }
    };

    // 127/8, 10/8, 172.16/12, 192.168/16: the reverse proxy on the same host,
    // or Docker's bridge in front of a container. A public address is never
    // believed about who the client is.
    inline bool private_ipv4(const std::string& ip)
    {
        in_addr a;
        if(inet_pton(AF_INET, ip.c_str(), &a)!=1)
        return false;
        uint32_t x = ntohl(a.s_addr);
        return (x>>24)==127 || (x>>24)==10 || (x>>20)==0xAC1 || (x>>16)==0xC0A8;
    }

    inline std::string trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
        return b==std::string::npos ? "" : s.substr(b, e-b+1);
    }

    inline bool valid_ip(const std::string& ip)
    {
        unsigned char buf[16];
        return inet_pton(AF_INET, ip.c_str(), buf)==1 || inet_pton(AF_INET6, ip.c_str(), buf)==1;
    }

    struct Client
    {
        std::string ip;
        bool https = false;
    };

    // Who sent a request. With trust_proxy and a private peer (the proxy), the
    // proxy's X-Real-IP, else the last X-Forwarded-For entry — the one the
    // proxy appended; earlier entries are whatever the client wrote — and
    // X-Forwarded-Proto. Otherwise the peer itself, over plain http.
    inline Client client_of(const std::string& peer, const std::map<std::string, std::string>& headers, bool trust_proxy)
    {
        Client c;
        c.ip = peer;
        if(!trust_proxy || !private_ipv4(peer))
        return c;
        auto real = headers.find("x-real-ip");
        auto forwarded = headers.find("x-forwarded-for");
        if(real!=headers.end() && valid_ip(trim(real->second)))
        c.ip = trim(real->second);
        else if(forwarded!=headers.end())
        {
            size_t comma = forwarded->second.rfind(',');
            std::string last = trim(comma==std::string::npos ? forwarded->second : forwarded->second.substr(comma+1));
            if(valid_ip(last))
            c.ip = last;
        }
        auto proto = headers.find("x-forwarded-proto");
        c.https = proto!=headers.end() && trim(proto->second)=="https";
        return c;
    }

    // Where to go after logging in: a path on this server, or "/". Only the
    // characters this GUI's own links use, so neither another site ("//x",
    // "/\x") nor a header break can get through.
    inline std::string safe_next(const std::string& next)
    {
        if(next.size()<1 || next.size()>256 || next[0]!='/' || (next.size()>1 && (next[1]=='/' || next[1]=='\\')))
        return "/";
        for(char c : next)
        if(!(isalnum((unsigned char)c) || std::string("/?&=_.-").find(c)!=std::string::npos))
        return "/";
        if(next.compare(0, 6, "/login")==0 || next.compare(0, 7, "/logout")==0)
        return "/";
        return next;
    }

    inline std::string url_encode(const std::string& s)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string out;
        for(unsigned char c : s)
        if(isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~')
        out += (char)c;
        else
        {
            out += '%';
            out += digits[c>>4];
            out += digits[c&0xF];
        }
        return out;
    }

    inline std::string html_escape(const std::string& s)
    {
        std::string out;
        for(char c : s)
        switch(c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
        return out;
    }
}

#endif // GUI_ACCESS_HPP
