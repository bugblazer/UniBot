#define CROW_MAIN
#include "crow/crow.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;

struct FAQ
{
    string question;
    string answer;
    vector<string> keywords;
};

vector<FAQ> faqs;
string admin_user, admin_pass;

//Admin writing:
void saveFAQs(const string& filename){
    ofstream file(filename);
    for(const auto& faq: faqs){
        file<<"Q:"<<faq.question<<endl;
        file<<"A:"<<faq.answer<<endl;
        file<<"K:";
        for(const auto& keyword: faq.keywords){
            file<<keyword<<" ";
        }
        file<<"\n";
    }
}

//Read faqs.txt file:
void loadFAQs(const string& filename){
    ifstream file(filename);
    faqs.clear();
    FAQ faq;
    string line;

    while(getline(file, line)){
        if (line.find("Q:")==0){
            faq.question=line.substr(2);
        }
        else if(line.find("A:")==0){
            faq.answer=line.substr(2);
        }
        else if(line.find("K:")==0){
            faq.keywords.clear();
            string keyword;
            istringstream key(line.substr(2));
            while(key>>keyword){
                faq.keywords.push_back(keyword);
            }
            faqs.push_back(faq);
        }
        
    }
    cout<<"Loaded "<<faqs.size()<<" faqs"<<endl;
}

//Admin login info:
void loadLoginInfo(const string& filename){
    ifstream file(filename);
    getline(file, admin_user);
    getline(file, admin_pass);
    cout<<"Loaded admin login info."<<endl;
}

//Matching keywords:
int matchFAQ(const string& user_input){
    int best_index=-1;
    int max_matches=0;
    for(int i=0; i<faqs.size(); i++){
        int match_count=0;
        for(const auto& keyword: faqs[i].keywords){
            if(user_input.find(keyword)!=string::npos){
                match_count++;
            }
        }
        if (match_count>max_matches){
            max_matches=match_count;
            best_index=i;
        }
        
    }
    return best_index;
}

//Main:
int main(){
    loadLoginInfo("backend/data/admin.txt");
    loadFAQs("backend/data/faqs.txt");

    crow::SimpleApp app;

    CROW_ROUTE(app, "/ask").methods("POST"_method)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Invalid JSON");

        string userMsg = body["question"].s();
        transform(userMsg.begin(), userMsg.end(), userMsg.begin(), ::tolower);

        int index = matchFAQ(userMsg);
        crow::json::wvalue res;
        if (index != -1)
            res["answer"] = faqs[index].answer;
        else
            res["answer"] = "Sorry, I couldn't find an answer. Please contact admin at +92 316 7695708";

        return crow::response{res};
    });

    CROW_ROUTE(app, "/admin/login").methods("POST"_method)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Invalid JSON");

        string username = body["username"].s();
        string password = body["password"].s();

        if (username == admin_user && password == admin_pass)
            return crow::response{200};
        else
            return crow::response(401, "Unauthorized");
    });

    CROW_ROUTE(app, "/admin/faqs").methods("GET"_method)
    ([] {
        crow::json::wvalue res;
        for (int i = 0; i < faqs.size(); ++i) {
            res[i]["question"] = faqs[i].question;
            res[i]["answer"] = faqs[i].answer;
            res[i]["keywords"] = faqs[i].keywords;
        }
        return crow::response{res};
    });

    CROW_ROUTE(app, "/admin/faqs").methods("POST"_method)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "Invalid JSON");

        FAQ newFAQ;
        newFAQ.question = body["question"].s();
        newFAQ.answer = body["answer"].s();
        for (auto& kw : body["keywords"]) newFAQ.keywords.push_back(kw.s());

        faqs.push_back(newFAQ);
        saveFAQs("backend/data/faqs.txt");

        return crow::response(201);
    });

    CROW_ROUTE(app, "/admin/faqs/<int>").methods("PUT"_method)
    ([](const crow::request& req, int index) {
        if (index < 0 || index >= faqs.size()) return crow::response(404);

        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400);

        faqs[index].question = body["question"].s();
        faqs[index].answer = body["answer"].s();
        faqs[index].keywords.clear();
        for (auto& kw : body["keywords"]) faqs[index].keywords.push_back(kw.s());

        saveFAQs("backend/data/faqs.txt");
        return crow::response(200);
    });

    CROW_ROUTE(app, "/admin/faqs/<int>").methods("DELETE"_method)
    ([](int index) {
        if (index < 0 || index >= faqs.size()) return crow::response(404);
        faqs.erase(faqs.begin() + index);
        saveFAQs("backend/data/faqs.txt");
        return crow::response(200);
    });

    app.port(18080).multithreaded().run();
}