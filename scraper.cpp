#include <iostream>
#include <string>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <map>
#include <stdexcept>
#include <array>
#include <fstream>
#include <cctype>
#include <boost/asio.hpp>
#include <Windows.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <thread>
#include <chrono>
#include <semaphore>
#include <mutex>

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    DWORD bytesRead;
    HANDLE pipeRead, pipeWrite;
    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    if (!CreatePipe(&pipeRead, &pipeWrite, &saAttr, 0)) {
        throw std::runtime_error("CreatePipe() failed!");
    }

    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = pipeWrite;
    siStartInfo.hStdOutput = pipeWrite;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    if (!CreateProcess(nullptr, (LPSTR)cmd, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &siStartInfo, &piProcInfo)) {
        CloseHandle(pipeRead);
        CloseHandle(pipeWrite);
        throw std::runtime_error("CreateProcess() failed!");
    }

    CloseHandle(pipeWrite);

    while (true) {
        if (!ReadFile(pipeRead, buffer.data(), buffer.size(), &bytesRead, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break; // Pipe zakończony, zakończ odczyt
            } else {
                throw std::runtime_error("ReadFile() failed!");
            }
        }
        if (bytesRead == 0) {
            break; // Nie ma więcej danych do odczytu
        }
        result.append(buffer.data(), bytesRead);
    }

    CloseHandle(pipeRead);
    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);

    return result;
}

std::string removeSpaces(const std::string& input) {
    std::string output = input;
    output.erase(std::remove(output.begin(), output.end(), ' '), output.end());
    return output;
}

std::string parseHTML_KSEtable(const std::string& htmlContent) {
    // Parsowanie HTML
    htmlDocPtr doc = htmlReadDoc((const xmlChar*)htmlContent.c_str(), NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

    // Utworzenie kontekstu XPath
    xmlXPathContextPtr context = xmlXPathNewContext(doc);

    // Zdefiniowanie wyrażenia XPath
    const xmlChar* xpathExpr = reinterpret_cast<const xmlChar*>("//table[@class='legend-table']/tbody/tr");

    std::string output;

    // Ocena wyrażenia XPath
    xmlXPathObjectPtr result = xmlXPathEvalExpression(xpathExpr, context);
    if (result != nullptr) {
        xmlNodeSetPtr nodes = result->nodesetval;
        for (int i = 0; i < nodes->nodeNr; ++i) {
            xmlNodePtr trNode = nodes->nodeTab[i];

            // Iteracja przez dzieci węzła <tr>
            xmlNodePtr child = trNode->children;
            int n=0;
            while (child != nullptr) {
                // Sprawdzenie, czy dziecko jest elementem <td>
                if (child->type == XML_ELEMENT_NODE && xmlStrEqual(child->name, BAD_CAST "td")) {
                    // Wypisanie zawartości węzła
                    xmlChar* content = xmlNodeGetContent(child);
                    if (content != nullptr) {
                        // output += reinterpret_cast<char*>(content);
                        std::string textContent = reinterpret_cast<char*>(content);
                        // std::cout<<textContent<<std::endl;
                        output += removeSpaces(textContent);
                        n++;
                        if (n==2){
                            output += "; "; // Separator
                            n=0;
                        }
                        else{
                            output += " ";
                        }
                        
                        xmlFree(content);
                    }
                }
                child = child->next;
            }
        }
        // std::cout << output << std::endl;
        xmlXPathFreeObject(result);
    } else {
        std::cerr << "Error: Unable to evaluate XPath expression." << std::endl;
    }

    // Zwolnienie pamięci
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    return output;
}

std::string parseHTML_KSEmap(const std::string& htmlContent) {
    // Parsowanie HTML
    htmlDocPtr doc = htmlReadDoc((const xmlChar*)htmlContent.c_str(), NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);

    // Utworzenie kontekstu XPath
    xmlXPathContextPtr context = xmlXPathNewContext(doc);

    std::pair<const char*, const char*> valueFields[] = {
        {"SE", "//span[@id='SE-value']"},
        {"LT", "//span[@id='LT-value']"},
        {"UA", "//span[@id='UA-value']"},
        {"SK", "//span[@id='SK-value']"},
        {"CZ", "//span[@id='CZ-value']"},
        {"DE", "//span[@id='DE-value']"},
    };
    std::string output;

    //iteracja po krajach w valueFields (pole wartości)
    for (const auto& field : valueFields) {
        const xmlChar* xpathExpr = BAD_CAST field.second;

        // Ocena wyrażenia XPath
        xmlXPathObjectPtr result = xmlXPathEvalExpression(xpathExpr, context);
        if (result != nullptr && xmlXPathNodeSetIsEmpty(result->nodesetval) == 0) {
            
            // Znaleziono wartość "PLAN" dla lokalizacji field.first
            xmlNodePtr node = result->nodesetval->nodeTab[0];
            xmlChar* content = xmlNodeGetContent(node);
            if (content != nullptr) {
                // std::cout << "Value for " << field.first << ": " << content << std::endl;
                std::string textContent = reinterpret_cast<char*>(content);
                // std::cout << "Value for " << field.first << ": " << textContent << std::endl;
                output += "VALUE_";
                output += field.first;
                output += " ";
                output += textContent;
                output += " ";
                xmlFree(content);
            }
            xmlChar* classAttr = xmlGetProp(node, BAD_CAST "class");
            if (classAttr != nullptr) {
                std::string classValue = reinterpret_cast<char*>(classAttr);
                // std::cout << "Class for " << field.first << ": " << classValue << std::endl;
                output += classValue;
                output += "; ";
                xmlFree(classAttr);
            } else {
                std::cerr << "Error: Unable to find class attribute for " << field.first << std::endl;
            }
        } else {
            std::cerr << "Error: Unable to find value for " << field.first << std::endl;
        }
        // Zwolnienie pamięci
        xmlXPathFreeObject(result);
    }

    // Zdefiniowanie wyrażeń XPath dla poszczególnych pól "PLAN"
    std::pair<const char*, const char*> planFields[] = {
        {"SE", "//span[@id='SE-plan-value']"},
        {"LT", "//span[@id='LT-plan-value']"},
        {"UA", "//span[@id='UA-plan-value']"},
        {"SK", "//span[@id='SK-plan-value']"},
        {"CZ", "//span[@id='CZ-plan-value']"},
        {"DE", "//span[@id='DE-plan-value']"},
    };

    // Iteracja po krajach w planFields (pole plan)
    for (const auto& field : planFields) {
        const xmlChar* xpathExpr = BAD_CAST field.second;

        // Ocena wyrażenia XPath
        xmlXPathObjectPtr result = xmlXPathEvalExpression(xpathExpr, context);
        if (result != nullptr && xmlXPathNodeSetIsEmpty(result->nodesetval) == 0) {
            // Znaleziono wartość "PLAN" dla lokalizacji field.first
            xmlNodePtr node = result->nodesetval->nodeTab[0];
            xmlChar* content = xmlNodeGetContent(node);
            if (content != nullptr) {
                std::string textContent = reinterpret_cast<char*>(content);
                // std::cout << "PLAN for " << field.first << ": " << textContent << std::endl;
                output += "PLAN_";
                output += field.first;
                output += " ";
                output += textContent;
                output += " ";
                xmlFree(content);
            }
            xmlChar* classAttr = xmlGetProp(node, BAD_CAST "class");
            if (classAttr != nullptr) {
                std::string classValue = reinterpret_cast<char*>(classAttr);
                // std::cout << "Class for " << field.first << ": " << classValue << std::endl;
                output += classValue;
                output += "; ";
                xmlFree(classAttr);
            } else {
                std::cerr << "Error: Unable to find class attribute for " << field.first << std::endl;
            }
        } else {
            std::cerr << "Error: Unable to find PLAN value for " << field.first << std::endl;
        }

        // Zwolnienie pamięci
        xmlXPathFreeObject(result);
    }

    // Zwolnienie pamięci
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);

    return output;
}

void saveContentToFile(const std::string& content, const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << content;
        file.close();
    } else {
        std::cerr << "can't open file to save" << std::endl;
    }
}

////////////////////////////////////////////////////////////////// tutaj zaczynamy funkcje wysyłania itp...

std::binary_semaphore readSem(1);  // Początkowo odblokowany do odczytu danych
std::binary_semaphore sendSem(0);  // Początkowo zablokowany do wysyłania danych

void readDataFromParserAndSave(std::string cmd){
    while (true) {
        std::string output = "\n";
        readSem.acquire(); // Oczekiwanie na odblokowanie do odczytu danych
        std::cout << "Blokuje semafor do odczytu danych" << std::endl;
        try {

            std::string result = exec(cmd.c_str());
            output += parseHTML_KSEtable(result) + "\n" + parseHTML_KSEmap(result);
            std::cout << "Odczytano dane ze strony" << std::endl;

            saveContentToFile(output, "output.txt");
            std::cout << "Zapisano dane do output.txt" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        std::cout << "Odczytałem dane - zwalniam semafor" << std::endl;
        sendSem.release();  // Odblokowanie semafora do wysyłania danych
    }
}

using boost::asio::ip::tcp;

void send_file(tcp::socket& socket, const std::string& filename) {
    try {
        // Otwieranie pliku do odczytu
        std::ifstream infile(filename, std::ios::binary);
        if (!infile) {
            std::cerr << "Error opening file for reading." << std::endl;
            return;
        }

        // Pobieranie rozmiaru pliku
        infile.seekg(0, std::ios::end);
        size_t file_size = infile.tellg();
        infile.seekg(0, std::ios::beg);

        // Wysyłanie rozmiaru pliku do serwera
        boost::asio::write(socket, boost::asio::buffer(&file_size, sizeof(file_size)));

        // Bufor dla danych
        std::vector<char> buffer(file_size);

        // Odczytywanie danych z pliku do bufora
        if (!infile.read(buffer.data(), file_size)) {
            std::cerr << "Error reading file." << std::endl;
            return;
        }

        // Wysyłanie danych do serwera
        boost::asio::write(socket, boost::asio::buffer(buffer));

        std::cout << "File sent successfully." << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void sendTCPIP() {
    while (true) {
        sendSem.acquire();  // Oczekiwanie na odblokowanie do wysyłania danych

        std::cout << "Blokuje semafor do wyslania danych" << std::endl;
        try {
            boost::asio::io_context io_context;
            tcp::socket socket(io_context);
            // tcp::endpoint endpoint(boost::asio::ip::make_address("192.168.191.16"), 1600);
            // tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), 1500);
            tcp::endpoint endpoint(boost::asio::ip::make_address("10.241.244.138"), 1468);
            socket.connect(endpoint);

            // Wysyłanie pliku
            send_file(socket, "output.txt");

            // Zamykanie gniazda
            socket.close();
            std::cout << "Wyslalem dane - zwalniam semafor" << std::endl;
        } catch (std::exception& e) {
            // std::lock_guard<std::mutex> lock(coutMutex);
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        readSem.release();  // Odblokowanie semafora do odczytu danych
        std::this_thread::sleep_for(std::chrono::minutes(10));  // Czekanie 20 minut po zakończeniu cyklu
    }
}

int main() {
    std::string cmd =  "C:/dev/phantomjs-2.1.1-windows/bin/phantomjs render_page.js";

    std::thread worker(readDataFromParserAndSave, cmd);
    std::thread worker2(sendTCPIP);

    worker.join();
    worker2.join();

    return 0;
}