// UniBot backend: keyword-matched FAQ answers, dated schedules, and an admin API.
// Run from the repository root so the relative data/web paths resolve.
#define CROW_MAIN
#include "crow_all.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

const string DATA_DIR = "backend/data/";
const string WEB_DIR = "webgui/";
const string FAQ_FILE = DATA_DIR + "faqs.txt";
const string SCHEDULE_FILE = DATA_DIR + "schedules.txt";
const string ADMIN_FILE = DATA_DIR + "admin.txt";

const size_t MAX_QUESTION = 300;
const size_t MAX_ANSWER = 2000;
const size_t MAX_KEYWORDS = 20;
const size_t MAX_KEYWORD = 40;
const size_t MAX_USER_MESSAGE = 500;
const auto SESSION_TTL = chrono::hours(8);

struct FAQ
{
    string question;
    string answer;
    vector<string> keywords;
};

struct Schedule
{
    string title;
    string date;  // YYYY-MM-DD
    string time;  // optional, free text such as "10:00 AM"
    string details;
    vector<string> keywords;
};

vector<FAQ> faqs;
vector<Schedule> schedules;
mutex dataMutex;

string admin_user, admin_pass;
string contact_message;

map<string, chrono::steady_clock::time_point> sessions;
mutex sessionMutex;

// ---------- helpers ----------

string toLower(string s)
{
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
    return s;
}

string trim(const string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Fields are stored one per line, so a stray newline would corrupt the data file.
string singleLine(string s)
{
    replace(s.begin(), s.end(), '\n', ' ');
    replace(s.begin(), s.end(), '\r', ' ');
    return trim(s);
}

vector<string> splitWords(const string& line)
{
    vector<string> words;
    istringstream in(line);
    string word;
    while (in >> word) words.push_back(word);
    return words;
}

// Keywords from the data files, lowercased and without repeats.
vector<string> uniqueKeywords(const string& line)
{
    vector<string> keywords;
    for (const auto& word : splitWords(toLower(line)))
        if (find(keywords.begin(), keywords.end(), word) == keywords.end()) keywords.push_back(word);
    return keywords;
}

string envOr(const char* name, const string& fallback)
{
    const char* value = getenv(name);
    return value && *value ? string(value) : fallback;
}

string today()
{
    time_t now = time(nullptr);
    tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
    return buf;
}

bool isIsoDate(const string& s)
{
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    int month = stoi(s.substr(5, 2)), day = stoi(s.substr(8, 2));
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

// Compares in constant time so response timing doesn't leak how much of a secret matched.
bool safeEquals(const string& a, const string& b)
{
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < max(a.size(), b.size()); i++)
        diff |= static_cast<unsigned char>((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0));
    return diff == 0;
}

string newToken()
{
    static random_device rd;
    static mt19937_64 gen(rd());
    ostringstream out;
    for (int i = 0; i < 4; i++) out << hex << gen();
    return out.str();
}

// ---------- persistence ----------

void saveFAQs()
{
    // Binary mode keeps "\n" line endings on Windows too, so data files diff cleanly.
    ofstream file(FAQ_FILE, ios::binary);
    for (const auto& faq : faqs)
    {
        file << "Q:" << faq.question << "\n";
        file << "A:" << faq.answer << "\n";
        file << "K:";
        for (const auto& keyword : faq.keywords) file << keyword << " ";
        file << "\n";
    }
}

void loadFAQs()
{
    ifstream file(FAQ_FILE);
    faqs.clear();
    FAQ faq;
    string line;
    while (getline(file, line))
    {
        if (line.rfind("Q:", 0) == 0) faq.question = singleLine(line.substr(2));
        else if (line.rfind("A:", 0) == 0) faq.answer = singleLine(line.substr(2));
        else if (line.rfind("K:", 0) == 0)
        {
            faq.keywords = uniqueKeywords(line.substr(2));
            faqs.push_back(faq);
            faq = FAQ{};
        }
    }
    cout << "Loaded " << faqs.size() << " FAQs" << endl;
}

// Schedule records: T: title, D: date, W: time (optional), I: details, K: keywords (ends a record).
void saveSchedules()
{
    ofstream file(SCHEDULE_FILE, ios::binary);
    for (const auto& s : schedules)
    {
        file << "T:" << s.title << "\n";
        file << "D:" << s.date << "\n";
        file << "W:" << s.time << "\n";
        file << "I:" << s.details << "\n";
        file << "K:";
        for (const auto& keyword : s.keywords) file << keyword << " ";
        file << "\n";
    }
}

void loadSchedules()
{
    ifstream file(SCHEDULE_FILE);
    schedules.clear();
    Schedule s;
    string line;
    while (getline(file, line))
    {
        if (line.rfind("T:", 0) == 0) s.title = singleLine(line.substr(2));
        else if (line.rfind("D:", 0) == 0) s.date = singleLine(line.substr(2));
        else if (line.rfind("W:", 0) == 0) s.time = singleLine(line.substr(2));
        else if (line.rfind("I:", 0) == 0) s.details = singleLine(line.substr(2));
        else if (line.rfind("K:", 0) == 0)
        {
            s.keywords = uniqueKeywords(line.substr(2));
            if (isIsoDate(s.date)) schedules.push_back(s);
            s = Schedule{};
        }
    }
    cout << "Loaded " << schedules.size() << " schedules" << endl;
}

// Credentials come from the environment, or from a git-ignored admin.txt (username, then password).
void loadLoginInfo()
{
    admin_user = envOr("UNIBOT_ADMIN_USER", "");
    admin_pass = envOr("UNIBOT_ADMIN_PASS", "");
    if (admin_user.empty() || admin_pass.empty())
    {
        ifstream file(ADMIN_FILE);
        getline(file, admin_user);
        getline(file, admin_pass);
        admin_user = trim(admin_user);
        admin_pass = trim(admin_pass);
    }
    if (admin_user.empty() || admin_pass.empty())
        cout << "Admin login disabled: set UNIBOT_ADMIN_USER/UNIBOT_ADMIN_PASS or create " << ADMIN_FILE << endl;
    else
        cout << "Loaded admin login info." << endl;
}

// ---------- matching ----------

struct Match
{
    int index = -1;
    vector<string> matched;
};

// Splits a lowercased message into words. Bytes >= 128 count as letters so UTF-8 words stay whole.
vector<string> tokenize(const string& text)
{
    vector<string> words;
    string word;
    for (unsigned char c : text)
    {
        if (isalnum(c) || c >= 128) word += static_cast<char>(c);
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

// Whole-word match, plus a shared 6-letter prefix so "verified" matches "verification" and
// "challans" matches "challan". Single letters never match; they'd hit almost every sentence.
bool keywordMatches(const string& word, const string& keyword)
{
    if (keyword.size() < 2) return false;
    if (word == keyword) return true;
    return keyword.size() >= 6 && word.size() >= 6 && word.compare(0, 6, keyword, 0, 6) == 0;
}

template <typename T>
vector<string> matchedKeywords(const vector<string>& words, const T& item)
{
    vector<string> hits;
    for (const auto& keyword : item.keywords)
    {
        if (find(hits.begin(), hits.end(), keyword) != hits.end()) continue;
        for (const auto& word : words)
            if (keywordMatches(word, keyword))
            {
                hits.push_back(keyword);
                break;
            }
    }
    return hits;
}

template <typename T>
Match bestMatch(const vector<string>& words, const vector<T>& items)
{
    Match best;
    for (int i = 0; i < static_cast<int>(items.size()); i++)
    {
        auto hits = matchedKeywords(words, items[i]);
        if (hits.size() > best.matched.size())
        {
            best.index = i;
            best.matched = hits;
        }
    }
    return best;
}

// ---------- JSON ----------

crow::json::wvalue stringList(const vector<string>& items)
{
    crow::json::wvalue::list list;
    for (const auto& item : items) list.push_back(item);
    return crow::json::wvalue(list);
}

crow::json::wvalue faqJson(const FAQ& faq, int id)
{
    crow::json::wvalue v;
    v["id"] = id;
    v["question"] = faq.question;
    v["answer"] = faq.answer;
    v["keywords"] = stringList(faq.keywords);
    return v;
}

crow::json::wvalue scheduleJson(const Schedule& s, int id)
{
    crow::json::wvalue v;
    v["id"] = id;
    v["title"] = s.title;
    v["date"] = s.date;
    v["time"] = s.time;
    v["details"] = s.details;
    v["keywords"] = stringList(s.keywords);
    return v;
}

crow::response json(int code, crow::json::wvalue body)
{
    crow::response res(code, body);
    res.set_header("Content-Type", "application/json");
    res.set_header("Cache-Control", "no-store");
    return res;
}

crow::response error(int code, const string& message)
{
    crow::json::wvalue body;
    body["error"] = message;
    return json(code, std::move(body));
}

// ---------- validation ----------

vector<string> readKeywords(const crow::json::rvalue& list)
{
    vector<string> keywords;
    for (const auto& kw : list)
        for (const auto& word : splitWords(toLower(kw.s())))
            if (word.size() <= MAX_KEYWORD && find(keywords.begin(), keywords.end(), word) == keywords.end())
                keywords.push_back(word);
    return keywords;
}

// Reads a FAQ from a request body, returning an error message if it's invalid.
string readFAQ(const crow::json::rvalue& body, FAQ& out)
{
    if (!body.has("question") || !body.has("answer") || !body.has("keywords"))
        return "question, answer and keywords are required";
    out.question = singleLine(body["question"].s());
    out.answer = singleLine(body["answer"].s());
    out.keywords = readKeywords(body["keywords"]);
    if (out.question.empty() || out.answer.empty()) return "Question and answer can't be empty";
    if (out.question.size() > MAX_QUESTION) return "Question is too long";
    if (out.answer.size() > MAX_ANSWER) return "Answer is too long";
    if (out.keywords.empty()) return "Add at least one keyword";
    if (out.keywords.size() > MAX_KEYWORDS) return "Too many keywords";
    return "";
}

string readSchedule(const crow::json::rvalue& body, Schedule& out)
{
    if (!body.has("title") || !body.has("date") || !body.has("keywords"))
        return "title, date and keywords are required";
    out.title = singleLine(body["title"].s());
    out.date = singleLine(body["date"].s());
    out.time = body.has("time") ? singleLine(body["time"].s()) : "";
    out.details = body.has("details") ? singleLine(body["details"].s()) : "";
    out.keywords = readKeywords(body["keywords"]);
    if (out.title.empty()) return "Title can't be empty";
    if (out.title.size() > MAX_QUESTION || out.time.size() > 40 || out.details.size() > MAX_ANSWER)
        return "A field is too long";
    if (!isIsoDate(out.date)) return "Date must be YYYY-MM-DD";
    if (out.keywords.empty()) return "Add at least one keyword";
    if (out.keywords.size() > MAX_KEYWORDS) return "Too many keywords";
    return "";
}

// ---------- auth ----------

string bearerToken(const crow::request& req)
{
    const string& header = req.get_header_value("Authorization");
    return header.rfind("Bearer ", 0) == 0 ? header.substr(7) : "";
}

bool authorized(const crow::request& req)
{
    string token = bearerToken(req);
    if (token.empty()) return false;

    lock_guard<mutex> lock(sessionMutex);
    auto it = sessions.find(token);
    if (it == sessions.end()) return false;
    if (chrono::steady_clock::now() > it->second)
    {
        sessions.erase(it);
        return false;
    }
    return true;
}

// ---------- static files ----------

crow::response serveFile(const string& relative)
{
    // Only plain paths inside webgui/ are served.
    if (relative.find("..") != string::npos || relative.find('\\') != string::npos || relative.find(':') != string::npos)
        return crow::response(404);
    crow::response res;
    res.set_static_file_info(WEB_DIR + relative);
    if (res.code == 404) return crow::response(404);
    return res;
}

int main()
{
    loadLoginInfo();
    loadFAQs();
    loadSchedules();
    contact_message = envOr("UNIBOT_CONTACT",
                            "I couldn't find an answer to that. Try rephrasing your question, or contact the "
                            "admissions office for help.");

    crow::SimpleApp app;

    // ----- public -----

    CROW_ROUTE(app, "/")([] { return serveFile("index.html"); });
    CROW_ROUTE(app, "/admin")([] { return serveFile("admin.html"); });

    CROW_ROUTE(app, "/health")
    ([] {
        lock_guard<mutex> lock(dataMutex);
        crow::json::wvalue body;
        body["status"] = "ok";
        body["faqs"] = static_cast<int>(faqs.size());
        body["schedules"] = static_cast<int>(schedules.size());
        return json(200, std::move(body));
    });

    CROW_ROUTE(app, "/ask").methods("POST"_method)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body || !body.has("question")) return error(400, "Send a JSON body with a question");

        string message = toLower(trim(body["question"].s()));
        if (message.empty()) return error(400, "Question can't be empty");
        if (message.size() > MAX_USER_MESSAGE) message.resize(MAX_USER_MESSAGE);

        vector<string> words = tokenize(message);
        lock_guard<mutex> lock(dataMutex);
        Match faq = bestMatch(words, faqs);
        Match schedule = bestMatch(words, schedules);
        // Ties go to the FAQ; a schedule wins only when it matches strictly more keywords.
        bool useSchedule = schedule.index != -1 && schedule.matched.size() > faq.matched.size();
        bool useFAQ = !useSchedule && faq.index != -1;

        crow::json::wvalue res;
        if (useSchedule)
        {
            const auto& s = schedules[schedule.index];
            res["type"] = "schedule";
            res["answer"] = s.title + " is on " + s.date + (s.time.empty() ? "" : " at " + s.time) + "." +
                            (s.details.empty() ? "" : " " + s.details);
            res["schedule"] = scheduleJson(s, schedule.index);
            res["matched"] = stringList(schedule.matched);
        }
        else if (useFAQ)
        {
            res["type"] = "faq";
            res["answer"] = faqs[faq.index].answer;
            res["question"] = faqs[faq.index].question;
            res["matched"] = stringList(faq.matched);
        }
        else
        {
            res["type"] = "none";
            res["answer"] = contact_message;
            res["matched"] = stringList({});
        }

        // Up to three other FAQs that share keywords with the message, best first.
        vector<pair<size_t, int>> scored;
        for (int i = 0; i < static_cast<int>(faqs.size()); i++)
        {
            if (useFAQ && i == faq.index) continue;
            size_t hits = matchedKeywords(words, faqs[i]).size();
            if (hits > 0) scored.push_back({hits, i});
        }
        stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        vector<string> related;
        for (size_t i = 0; i < scored.size() && i < 3; i++) related.push_back(faqs[scored[i].second].question);
        res["related"] = stringList(related);

        return json(200, std::move(res));
    });

    CROW_ROUTE(app, "/suggestions")
    ([] {
        lock_guard<mutex> lock(dataMutex);
        vector<string> items;
        for (size_t i = 0; i < faqs.size() && items.size() < 4; i++) items.push_back(faqs[i].question);
        return json(200, stringList(items));
    });

    CROW_ROUTE(app, "/schedules")
    ([] {
        lock_guard<mutex> lock(dataMutex);
        string now = today();
        vector<int> upcoming;
        for (int i = 0; i < static_cast<int>(schedules.size()); i++)
            if (schedules[i].date >= now) upcoming.push_back(i);
        sort(upcoming.begin(), upcoming.end(), [](int a, int b) { return schedules[a].date < schedules[b].date; });

        crow::json::wvalue::list list;
        for (int i : upcoming) list.push_back(scheduleJson(schedules[i], i));
        return json(200, crow::json::wvalue(list));
    });

    // ----- admin auth -----

    CROW_ROUTE(app, "/admin/login").methods("POST"_method)
    ([](const crow::request& req) {
        if (admin_user.empty() || admin_pass.empty()) return error(503, "Admin login isn't configured on the server");
        auto body = crow::json::load(req.body);
        if (!body || !body.has("username") || !body.has("password"))
            return error(400, "Username and password are required");

        bool userOk = safeEquals(body["username"].s(), admin_user);
        bool passOk = safeEquals(body["password"].s(), admin_pass);
        if (!(userOk && passOk)) return error(401, "Incorrect username or password");

        string token = newToken();
        {
            lock_guard<mutex> lock(sessionMutex);
            sessions[token] = chrono::steady_clock::now() + SESSION_TTL;
        }
        crow::json::wvalue res;
        res["token"] = token;
        return json(200, std::move(res));
    });

    CROW_ROUTE(app, "/admin/logout").methods("POST"_method)
    ([](const crow::request& req) {
        string token = bearerToken(req);
        if (!token.empty())
        {
            lock_guard<mutex> lock(sessionMutex);
            sessions.erase(token);
        }
        return crow::response(204);
    });

    // ----- admin: FAQs -----

    CROW_ROUTE(app, "/admin/faqs").methods("GET"_method)
    ([](const crow::request& req) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        lock_guard<mutex> lock(dataMutex);
        crow::json::wvalue::list list;
        for (int i = 0; i < static_cast<int>(faqs.size()); i++) list.push_back(faqJson(faqs[i], i));
        return json(200, crow::json::wvalue(list));
    });

    CROW_ROUTE(app, "/admin/faqs").methods("POST"_method)
    ([](const crow::request& req) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        auto body = crow::json::load(req.body);
        if (!body) return error(400, "Invalid JSON");
        FAQ faq;
        string problem = readFAQ(body, faq);
        if (!problem.empty()) return error(400, problem);

        lock_guard<mutex> lock(dataMutex);
        faqs.push_back(faq);
        saveFAQs();
        return json(201, faqJson(faq, static_cast<int>(faqs.size()) - 1));
    });

    CROW_ROUTE(app, "/admin/faqs/<int>").methods("PUT"_method)
    ([](const crow::request& req, int index) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        auto body = crow::json::load(req.body);
        if (!body) return error(400, "Invalid JSON");
        FAQ faq;
        string problem = readFAQ(body, faq);
        if (!problem.empty()) return error(400, problem);

        lock_guard<mutex> lock(dataMutex);
        if (index < 0 || index >= static_cast<int>(faqs.size())) return error(404, "FAQ not found");
        faqs[index] = faq;
        saveFAQs();
        return json(200, faqJson(faq, index));
    });

    CROW_ROUTE(app, "/admin/faqs/<int>").methods("DELETE"_method)
    ([](const crow::request& req, int index) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        lock_guard<mutex> lock(dataMutex);
        if (index < 0 || index >= static_cast<int>(faqs.size())) return error(404, "FAQ not found");
        faqs.erase(faqs.begin() + index);
        saveFAQs();
        return crow::response(204);
    });

    // ----- admin: schedules -----

    CROW_ROUTE(app, "/admin/schedules").methods("GET"_method)
    ([](const crow::request& req) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        lock_guard<mutex> lock(dataMutex);
        crow::json::wvalue::list list;
        for (int i = 0; i < static_cast<int>(schedules.size()); i++) list.push_back(scheduleJson(schedules[i], i));
        return json(200, crow::json::wvalue(list));
    });

    CROW_ROUTE(app, "/admin/schedules").methods("POST"_method)
    ([](const crow::request& req) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        auto body = crow::json::load(req.body);
        if (!body) return error(400, "Invalid JSON");
        Schedule s;
        string problem = readSchedule(body, s);
        if (!problem.empty()) return error(400, problem);

        lock_guard<mutex> lock(dataMutex);
        schedules.push_back(s);
        saveSchedules();
        return json(201, scheduleJson(s, static_cast<int>(schedules.size()) - 1));
    });

    CROW_ROUTE(app, "/admin/schedules/<int>").methods("PUT"_method)
    ([](const crow::request& req, int index) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        auto body = crow::json::load(req.body);
        if (!body) return error(400, "Invalid JSON");
        Schedule s;
        string problem = readSchedule(body, s);
        if (!problem.empty()) return error(400, problem);

        lock_guard<mutex> lock(dataMutex);
        if (index < 0 || index >= static_cast<int>(schedules.size())) return error(404, "Schedule not found");
        schedules[index] = s;
        saveSchedules();
        return json(200, scheduleJson(s, index));
    });

    CROW_ROUTE(app, "/admin/schedules/<int>").methods("DELETE"_method)
    ([](const crow::request& req, int index) {
        if (!authorized(req)) return error(401, "Sign in again to continue");
        lock_guard<mutex> lock(dataMutex);
        if (index < 0 || index >= static_cast<int>(schedules.size())) return error(404, "Schedule not found");
        schedules.erase(schedules.begin() + index);
        saveSchedules();
        return crow::response(204);
    });

    // ----- web UI assets -----

    CROW_ROUTE(app, "/<path>")([](const string& path) { return serveFile(path); });

    int port = stoi(envOr("UNIBOT_PORT", "18080"));
    cout << "UniBot running at http://localhost:" << port << endl;
    app.port(port).multithreaded().run();
}
