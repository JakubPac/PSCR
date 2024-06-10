#include <iostream>
#include <curl/curl.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "rapidcsv.h"
#include <fstream>
#include <boost/asio.hpp>
#include <thread>
#include <string>
#include <vector>
#include <semaphore.h>

using boost::asio::ip::tcp;
sem_t semaphore;
int count_file_send = 0;
// Funkcja do zapisywania nagłówków do pliku CSV
void saveHeadersToCSV(const std::string& filename, const std::vector<std::string>& headers) {
    std::ofstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku do zapisu." << std::endl;
        return;
    }
    file << "\n";
    for (size_t i = 0; i < headers.size(); ++i) {
        file << headers[i];
        if (i != headers.size() - 1)
            file << ",";
    }
    file << std::endl;

    file.close();
    std::cout << "Nagłówki zapisano do pliku " << filename << std::endl;
}

// Funkcja do zapisywania danych kolumnami do pliku CSV
void appendDataToCSV(const std::string& filename, const std::vector<std::string>& data) {
    std::ofstream file(filename, std::ios_base::app); // Otwarcie pliku w trybie dopisywania

    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku do zapisu." << std::endl;
        return;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        file << data[i];
        if (i != data.size() - 1)
            file << ",";
    }
    file << std::endl;

    file.close();
    std::cout << "Dane zapisano do pliku " << filename << std::endl;
}


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
        count_file_send++;
        std::cout << "File sent successfully." << std::to_string(count_file_send) << std::endl;

        return;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

}

void wysylanie_tcp() {
    sem_wait(&semaphore);
    sem_close(&semaphore);
    try {

        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::endpoint endpoint(boost::asio::ip::make_address("10.241.26.179"), 1600);
        socket.connect(endpoint);

        // Wysyłanie pliku
        send_file(socket, "C:/Users/korne/CLionProjects/untitled2/pliki_csv/pobrane_dane.txt");

        // Zamykanie gniazda
        socket.close();
        sem_post(&semaphore);
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        sem_post(&semaphore);
    }
}


// Funkcja zapisująca dane z pobranego zasobu
size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *buffer) {
    buffer->append((char *)contents, size * nmemb);
    return size * nmemb;
}
std::vector<std::string> api_call(std::string lat,std::string lon) {
    CURL *curl;
    CURLcode res;
    std::string apiKey = "2c83712e036ef7317844137a2da70218"; // Wstaw swój klucz API OpenWeatherMap
    std::string open_weather_url =
            "https://api.openweathermap.org/data/2.5/weather?units=metric&lat=" + lat + "&lon=" + lon + "&appid=" +
            apiKey;

    // Inicjalizacja sesji cURL
    curl = curl_easy_init();
    if (curl) {
        // Ustawienie opcji URL
        curl_easy_setopt(curl, CURLOPT_URL, open_weather_url.c_str());
        // Ustawienie funkcji zapisującej dane
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        // Przekazanie bufora jako parametru do funkcji zapisującej dane
        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        // Wykonanie żądania
        res = curl_easy_perform(curl);

        // Sprawdzenie rezultatu zapytania
        if (res != CURLE_OK) {
            std::cerr << "Błąd podczas pobierania danych: " << curl_easy_strerror(res) << std::endl;
        } else {
            // Wyświetlanie danych
            boost::property_tree::ptree pt;
            std::istringstream is(buffer);
            boost::property_tree::read_json(is, pt);

            std::string miejsce = pt.get<std::string>("name");
            std::string temperatura = std::to_string(pt.get_child("main").get<float>("temp"));
            std::string zachmurzenie = std::to_string(pt.get_child("clouds").get<int>("all"));
            std::string predkosc_wiatru = std::to_string(pt.get_child("wind").get<float>("speed"));
            //                    std::cout << "Weather in " << miejsce << ":" << std::endl;
            //                    std::cout << "Temperatura: " << temperatura << " \370C" << std::endl;
            //                    std::cout << "Zachmurzenie: " << zachmurzenie << " %" << std::endl;
            //                    std::cout << "Predkosc wiatru: " << predkosc_wiatru << " m/s" << std::endl;
            //                    std::cout << "\n\n";

            return {lat, lon, miejsce, temperatura, zachmurzenie, predkosc_wiatru};


        }
        // Zwolnienie zasobów cURL
        curl_easy_cleanup(curl);
    }
    return {"0","0","Linia","0","0","0"};
}
void write_all_api_call(std::string plik_pogodowy_sciezka){
    sem_wait(&semaphore);
    sem_close(&semaphore);
    rapidcsv::Document doc("C:/Users/korne/CLionProjects/untitled2/pliki_csv/siatka50kmx50km_polska.csv");


    std::vector<std::string> lat = doc.GetColumn<std::string>("latitude");
    std::vector<std::string> lon = doc.GetColumn<std::string>("longitude");
    std::vector<std::string> naglowki = {"szerokosc", "dlugosc", "miejsce","temperatura","zachmurzenie","predkosc_wiatru"};
    saveHeadersToCSV(plik_pogodowy_sciezka, naglowki);
    for (int i = 0; i < lat.size(); i++) {
        std::vector<std::string> dane_pogodowe = api_call(lat[i],lon[i]);
        appendDataToCSV(plik_pogodowy_sciezka, dane_pogodowe);
    }

    sem_post(&semaphore);
}

int main() {

    std::string plik_pogodowy_sciezka= "C:/Users/korne/CLionProjects/untitled2/pliki_csv/pobrane_dane.txt";
//    std::thread worker(write_all_api_call,plik_pogodowy_sciezka);
//    std::thread worker2(wysylanie_tcp);
    sem_init(&semaphore, 0, 1);
    while(true) {
//        write_all_api_call(plik_pogodowy_sciezka);
//        sem_wait(&semaphore);

        std::thread worker(write_all_api_call,plik_pogodowy_sciezka);
        std::thread worker2(wysylanie_tcp);

        worker.join();
        worker2.join();

        std::this_thread::sleep_for(std::chrono::minutes(30));
//        sem_post(&semaphore);
    }
    return 0;
}
