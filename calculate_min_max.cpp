// dzialajacy kod z wprowadzeniem zmian
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <mutex>
#include <condition_variable>
#include <boost/asio.hpp>
#include <thread>
#include <set>
#include <utility>
#include <algorithm>
#include <limits>

using boost::asio::ip::tcp;

std::mutex mtx;
std::condition_variable cv;
bool fileReady = false;      // zm glob ktora mutx ja blokuje przez co dany watek wtedy moze 
bool resultsReady = false;    

// Funkcja usuwa spacje z początku i końca tekstu
void usunSpacje(std::string& tekst) {
    tekst.erase(0, tekst.find_first_not_of(" "));
    tekst.erase(tekst.find_last_not_of(" ") + 1);
}

// Funkcja usuwa słowa po cyfrach w tekście
void usunSlowaPoCyfrach(std::string& tekst) {
    bool znalezionoCyfre = false;
    for (size_t i = 0; i < tekst.size(); ++i) {
        if (std::isdigit(tekst[i])) {
            znalezionoCyfre = true;
        } else if (znalezionoCyfre && std::isalpha(tekst[i])) {
            size_t startPos = i;
            while (i < tekst.size() && std::isalpha(tekst[i])) {
                ++i;
            }
            tekst.erase(startPos, i - startPos);
            znalezionoCyfre = false;
        }
    }
}

// Funkcja przetwarza drugą linię pliku i zwraca kontenery
std::vector<std::string> przetworzDrugaLinie(const std::string& drugaLinia) {
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, []{ return fileReady; });     // Czeka na odebranie pliku
    }

    std::istringstream iss(drugaLinia);
    std::vector<std::string> kontenery;
    std::string token;

    // Przetwarza drugą linię i dodaje elementy zaczynające się od "el." do wektora kontenery
    while (std::getline(iss, token, ';')) {
        usunSpacje(token);
        if (token.substr(0, 3) == "el.") {
            kontenery.push_back(token);
        }
    }

    return kontenery;
}

// Funkcja przetwarza trzecią linię pliku i zwraca kontenery
std::vector<std::string> przetworzTrzeciaLinie(std::string trzeciaLinia) {
    usunSlowaPoCyfrach(trzeciaLinia);
    std::istringstream iss(trzeciaLinia);
    std::vector<std::string> kontenery;
    std::string token;

    // Przetwarza trzecią linię i dodaje elementy do wektora kontenery
    while (std::getline(iss, token, ';')) {
        usunSpacje(token);
        kontenery.push_back(token);
    }

    return kontenery;
}

// Funkcja znajduje maksymalną wartość w kontenerach i zwraca ją wraz z kluczami
std::pair<std::set<std::string>, int> znajdzMaxWartosc(const std::vector<std::string>& kontenery) {
    int maxWartosc = std::numeric_limits<int>::min();
    std::set<std::string> maxKlucze;

    // Przetwarza kontenery w celu znalezienia maksymalnej wartości i kluczy
    for (const auto& kontener : kontenery) {
        std::istringstream iss(kontener);
        std::string klucz;
        int wartosc;

        if (iss >> klucz >> wartosc) {
            if (wartosc > maxWartosc) {
                maxWartosc = wartosc;
                maxKlucze.clear();
                maxKlucze.insert(klucz);
            } else if (wartosc == maxWartosc) {
                maxKlucze.insert(klucz);
            }
        }
    }

    return std::make_pair(maxKlucze, maxWartosc);
}

// Funkcja znajduje maksymalną i minimalną wartość w kontenerach i zwraca je wraz z kluczami
std::pair<std::pair<std::set<std::string>, int>, std::pair<std::set<std::string>, int>> znajdzMaxMinWartosc(const std::vector<std::string>& kontenery) {
    int maxWartosc = std::numeric_limits<int>::min();
    int minWartosc = std::numeric_limits<int>::max();
    std::set<std::string> maxKlucze;
    std::set<std::string> minKlucze;

    // Przetwarza kontenery w celu znalezienia maksymalnej i minimalnej wartości oraz kluczy
    for (const auto& kontener : kontenery) {
        std::istringstream iss(kontener);
        std::string klucz;
        int wartosc;

        if (iss >> klucz >> wartosc) {
            if (wartosc > maxWartosc) {
                maxWartosc = wartosc;
                maxKlucze.clear();
                maxKlucze.insert(klucz);
            } else if (wartosc == maxWartosc) {
                maxKlucze.insert(klucz);
            }
            if (wartosc < minWartosc) {
                minWartosc = wartosc;
                minKlucze.clear();
                minKlucze.insert(klucz);
            } else if (wartosc == minWartosc) {
                minKlucze.insert(klucz);
            }
        }
    }

    return std::make_pair(std::make_pair(maxKlucze, maxWartosc), std::make_pair(minKlucze, minWartosc));
}

// Funkcja obsługująca połączenie TCP
void obslugaPolaczenia(tcp::socket& sock) {
    try {
        boost::asio::streambuf bufOdbiorczy;
        boost::system::error_code kodBledu;

        size_t dlugosc = boost::asio::read(sock, bufOdbiorczy, kodBledu);      // Odczyt danych z połączenia

        if (kodBledu && kodBledu != boost::asio::error::eof) {
            std::cerr << "Blad przy odbiorze danych: " << kodBledu.message() << std::endl;
            return;
        }

        std::ofstream plikOut("wyjscie2.txt", std::ios::binary);    // Zapis danych do pliku
        if (!plikOut) {
            std::cerr << "Blad przy otwieraniu pliku do zapisu." << std::endl;
            return;
        }

        const char* dane = boost::asio::buffer_cast<const char*>(bufOdbiorczy.data());
        size_t rozmiar = bufOdbiorczy.size();
        plikOut.write(dane, rozmiar);       // Zapis odebranych danych do pliku

        std::cout << "Plik odebrany i zapisany." << std::endl;
        std::cout << "\n";
        {
            std::lock_guard<std::mutex> lock(mtx);
            fileReady = true;               // Ustawienie flagi fileReady na true
        }
        cv.notify_one();                    // Powiadomienie o odebraniu pliku
    } catch (std::exception& ex) {
        std::cerr << "Wyjatek: " << ex.what() << std::endl;
    }
}

// Funkcja nasłuchująca połączeń TCP
void nasluchujPolaczen() {
    try {
        boost::asio::io_context kontekst;
        tcp::endpoint punktKonc(boost::asio::ip::make_address("10.241.244.138"), 1468); // Adres IP i port do nasłuchu
        tcp::acceptor akceptor(kontekst, punktKonc);

        while (true) {
            tcp::socket sock(kontekst);
            akceptor.accept(sock);      // Akceptacja połączenia

            obslugaPolaczenia(sock);    // Obsługa połączenia
        }
    } catch (std::exception& ex) {
        std::cerr << "Wyjatek: " << ex.what() << std::endl;
    }
}

// Funkcja obliczająca maksymalną i minimalną wartość
int obliczMaxMin() {
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, []{ return fileReady; });     // Czeka na odebranie pliku
    }

    std::ifstream plik("wyjscie2.txt");
    if (!plik.is_open()) {
        std::cerr << "Nie można otworzyć pliku." << std::endl;
        return 1;
    }

    std::string pustaLinia, drugaLinia, trzeciaLinia;

    // Odczyt trzech linii z pliku
    if (std::getline(plik, pustaLinia) && std::getline(plik, drugaLinia) && std::getline(plik, trzeciaLinia)) {
    } else {
        std::cerr << "Nie udało się odczytać trzech linii z pliku." << std::endl;
    }
    std::cout << "Pierwsza linia: " << pustaLinia << std::endl;
    std::cout << "\n";
    std::cout << "Druga linia: " << drugaLinia << std::endl;
    std::cout << "\n";
    std::cout << "Trzecia linia: " << trzeciaLinia << std::endl;
    std::cout << "\n";

    // Przetwarzanie drugiej i trzeciej linii
    auto konteneryDrugaLinia = przetworzDrugaLinie(drugaLinia);
    auto konteneryTrzeciaLinia = przetworzTrzeciaLinie(trzeciaLinia);

    // Znajdowanie maksymalnej wartości
    auto paraMaxWartosc = znajdzMaxWartosc(konteneryDrugaLinia);
    // Znajdowanie maksymalnej i minimalnej wartości
    auto paraMaxMinWartosc = znajdzMaxMinWartosc(konteneryTrzeciaLinia);

    // Zapis wyników do pliku
    std::ofstream plikOut("calculateminmax.txt");
    if (!plikOut.is_open()) {
        std::cerr << "Nie można otworzyć pliku." << std::endl;
        return 1;
    }

    plik.clear();
    plik.seekg(0, std::ios::beg);
    plikOut << plik.rdbuf();
    plikOut << "\n";

    // Zapis maksymalnych wartości
    for (const auto& klucz : paraMaxWartosc.first) {
        plikOut << klucz << " - " << paraMaxWartosc.second << std::endl;
        std::cout << klucz << " - " << paraMaxWartosc.second << std::endl;
    }

    // Zapis maksymalnych i minimalnych wartości
    for (const auto& klucz : paraMaxMinWartosc.first.first) {
        plikOut << klucz << " - " << paraMaxMinWartosc.first.second << std::endl;
        std::cout << klucz << " - " << paraMaxMinWartosc.first.second << std::endl;
    }

    for (const auto& klucz : paraMaxMinWartosc.second.first) {
        plikOut << klucz << " - " << paraMaxMinWartosc.second.second << std::endl;
        std::cout << klucz << " - " << paraMaxMinWartosc.second.second << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        fileReady = false;      // Reset flagi fileReady po zapisie pliku
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        resultsReady = true;    // Ustawienie flagi resultsReady na true w celu poinformowania o przetworzeniu wartosci z pliku
    }
    cv.notify_one();            // Powiadomienie o gotowości wyników

    return 0;
}

// Funkcja wysyłająca plik przez TCP
void wyslijPlik(tcp::socket& sock, const std::string& nazwaPliku) {
    try {
        std::ifstream plikIn(nazwaPliku, std::ios::binary);
        if (!plikIn) {
            std::cerr << "Blad przy otwieraniu pliku do odczytu." << std::endl;
            return;
        }

        plikIn.seekg(0, std::ios::end);
        size_t rozmiarPliku = plikIn.tellg();
        plikIn.seekg(0, std::ios::beg);

        // Wysyłanie rozmiaru pliku
        boost::asio::write(sock, boost::asio::buffer(&rozmiarPliku, sizeof(rozmiarPliku)));

        std::vector<char> bufor(rozmiarPliku);

        if (!plikIn.read(bufor.data(), rozmiarPliku)) {
            std::cerr << "Blad przy odczycie pliku." << std::endl;
            return;
        }

        // Wysyłanie zawartości pliku
        boost::asio::write(sock, boost::asio::buffer(bufor));

        std::cout << "Plik wyslany pomyslnie." << std::endl;
        std::cout << "\n";
    } catch (std::exception& ex) {
        std::cerr << "Wyjatek: " << ex.what() << std::endl;
    }
}

// Funkcja do wysyłania plików w pętli
void wysylaniePlikow() {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, []{ return resultsReady; });  // Czeka na gotowość wyników
            }

            boost::asio::io_context kontekst;
            tcp::socket sock(kontekst);
            tcp::endpoint punktKonc(boost::asio::ip::make_address("10.241.226.85"), 1800);
            sock.connect(punktKonc);    // Nawiązanie połączenia

            wyslijPlik(sock, "calculateminmax.txt");    // Wysyłanie pliku

            sock.close();

            {
                std::lock_guard<std::mutex> lock(mtx);
                resultsReady = false;               // Reset flagi resultsReady po wyslaniu pliku
            }
        }
    } catch (std::exception& ex) {
        std::cerr << "Wyjatek: " << ex.what() << std::endl;
    }
}

// Główna funkcja programu
int main() {
    std::thread watekOdbioru(nasluchujPolaczen);    // Uruchomienie wątku nasłuchującego połączenia
    watekOdbioru.detach();

    std::thread watekWysylki(wysylaniePlikow);      // Uruchomienie wątku wysyłającego pliki
    watekWysylki.detach();

    while (true) {
        obliczMaxMin();                     // Obliczanie maksymalnych i minimalnych wartości w pętli
    }

    return 0;
}
