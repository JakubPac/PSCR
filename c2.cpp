#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <mutex>
#include <condition_variable>
#include <asio.hpp>
#include <thread>

using asio::ip::tcp;

std::mutex m;
std::condition_variable cv;
bool fileReceived = false; // Flaga informująca, czy plik został odebrany
bool resultsProcessed = false; // Flaga informująca, czy wyniki zostały przetworzone

void handle_connection(tcp::socket& socket) {
    try {
        asio::streambuf receive_buffer;
        asio::error_code error;

        // Odbieranie danych
        size_t len = asio::read(socket, receive_buffer, error);

        if (error && error != asio::error::eof) {
            std::cerr << "Error receiving data: " << error.message() << std::endl;
            return;
        }

        // Zapisywanie danych do pliku
        std::ofstream outfile("pobrane2.txt", std::ios::binary);
        if (!outfile) {
            std::cerr << "Error opening file for writing." << std::endl;
            return;
        }

        // Pobieranie wskaźnika na dane w buforze
        const char* data = asio::buffer_cast<const char*>(receive_buffer.data());
        // Pobieranie rozmiaru danych w buforze
        size_t size = receive_buffer.size();
        // Zapisywanie danych do pliku
        outfile.write(data, size);

        std::cout << "File received and saved." << std::endl;
        {
            std::lock_guard<std::mutex> lock(m);
            fileReceived = true; // Ustaw flagę na true po odebraniu pliku
        }
        cv.notify_one(); // Powiadom oczekujący wątek o odebraniu pliku
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void odbieranie() {
    try {
        asio::io_context io_context;
        tcp::endpoint endpoint(asio::ip::make_address("10.241.26.179"), 1600);
        tcp::acceptor acceptor(io_context, endpoint);

        while (true) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            handle_connection(socket);
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int srednia(){
    // Czekaj na otrzymanie pliku
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, []{ return fileReceived; });
    }

    std::ifstream file("pobrane2.txt");
    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku." << std::endl;
        return 1;
    }

    // Pomijanie pierwszych dwóch linii
    std::string dummy;
    for (int i = 0; i < 2; ++i) {
        if (!std::getline(file, dummy)) {
            std::cerr << "Plik ma mniej niż dwa wiersze." << std::endl;
            return 1;
        }
    }

    int num_rows = 0;
    std::vector<std::vector<double>> columns(3); // Wektor wektorów przechowujący wartości kolumn

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        int col = 0;

        while (std::getline(iss, token, ',')) {
            if (col >= 3) { // Pominięcie pierwszych trzech kolumn
                try {
                    double value = std::stod(token); // Konwersja na double
                    columns[col - 3].push_back(value); // Dodanie wartości do odpowiedniej kolumny
                } catch (const std::invalid_argument& e) {
                    std::cerr << "Błąd konwersji na double: " << e.what() << std::endl;
                    std::cerr << "Linia: " << line << std::endl;
                    return 1;
                }
            }
            col++;
        }
        num_rows++;
    }

    if (num_rows == 0) {
        std::cerr << "Brak danych po pierwszych dwóch wierszach." << std::endl;
        return 1;
    }

    int num_cols = 3; // Liczba ostatnich kolumn

    // Obliczanie średnich
    std::vector<double> averages(3);
    for (int i = 0; i < 3; ++i) {
        double sum = 0.0;
        for (double value : columns[i]) {
            sum += value;
        }
        averages[i] = sum / columns[i].size();
    }

    std::ofstream output_file("srednie.txt"); // Otwarcie pliku do zapisu

    // Zapisanie oryginalnej zawartości pliku "pobrane2.txt"
    file.clear();
    file.seekg(0, std::ios::beg);
    output_file << file.rdbuf();

    // Zapisanie średnich wartości na końcu pliku
    output_file << "Srednie:\n";
    for(int i=0;i<3;i++){
        output_file << "0,";
    }
    for (double avg : averages) {
        output_file << std::fixed << std::setprecision(3) <<avg<<",";
    }
    output_file << std::endl;

    std::cout << "Srednie zostały zapisane w pliku 'srednie.txt'" << std::endl;

    // Resetuj flagę po zapisaniu pliku
    {
        std::lock_guard<std::mutex> lock(m);
        fileReceived = false;
    }

    // Ustaw flagę informującą o przetworzeniu wyników
    {
        std::lock_guard<std::mutex> lock(m);
        resultsProcessed = true;
    }
    cv.notify_one(); // Powiadom wątek wysyłający o przetworzeniu wyników

    return 0;
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
        asio::write(socket, asio::buffer(&file_size, sizeof(file_size)));

        // Bufor dla danych
        std::vector<char> buffer(file_size);

        // Odczytywanie danych z pliku do bufora
        if (!infile.read(buffer.data(), file_size)) {
            std::cerr << "Error reading file." << std::endl;
            return;
        }

        // Wysyłanie danych do serwera
        asio::write(socket, asio::buffer(buffer));

        std::cout << "File sent successfully." << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void wysyłanie(){
    try {
        while (true) {
            // Czekaj na przetworzenie wyników
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, []{ return resultsProcessed; });
            }

            asio::io_context io_context;
            tcp::socket socket(io_context);
            tcp::endpoint endpoint(asio::ip::make_address("10.241.226.85"), 1500);
            socket.connect(endpoint);

            // Wysyłanie pliku
            send_file(socket, "C:/Users/jakub/Desktop/test/srednie.txt");

            // Zamykanie gniazda
            socket.close();

            // Resetuj flagę po wysłaniu pliku
            {
                std::lock_guard<std::mutex> lock(m);
                resultsProcessed = false;
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    std::thread receiverThread(odbieranie);
    receiverThread.detach(); // Nie czekaj na zakończenie wątku odbierającego

    std::thread senderThread(wysyłanie);
    senderThread.detach(); // Nie czekaj na zakończenie wątku wysyłającego

    while (true) {
        srednia(); // Wykonaj przetwarzanie pliku po jego otrzymaniu
    }

    return 0;
}
