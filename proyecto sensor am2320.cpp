#include "mbed.h"
#include <ctime>     // Para time()
#include <cmath>     // Para funciones matemáticas como log()
#include "Adafruit_GFX.h" //Librería para manejar gráficos en pantallas OLED
#include "Adafruit_SSD1306.h" // Librería para manejar pantallas OLED SSD1306

// Alias y defines
#define escritura       0x40
#define poner_brillo    0x88
#define dir_display     0xC0

// Pines para la comunicación I2C y para el NTC
I2C i2c(I2C_SDA, I2C_SCL);//d14:d15
AnalogIn ntc_sensor(A0);  // Conexión del termistor NTC al pin A0

// Pines para el display de 7 segmentos
DigitalOut sclk(D2);
DigitalInOut dio(D3);

// Tabla para convertir dígitos en segmentos del display
const char digitToSegment[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

// Dirección del sensor AM2320
const int AM2320_ADDRESS = 0x5C << 1;

// Configuración OLED SSD1306
Adafruit_SSD1306_I2c oled(i2c, D0);

// Constantes para el cálculo de la temperatura con el termistor NTC
const float Rfija = 10000.0;  // Resistencia de referencia en ohmios (10K)
const float Vin = 3.3;  // Voltaje de alimentación del divisor (3.3V)

// Coeficientes de la ecuación de Steinhart-Hart para el NTC
const float A = 1.009249522e-03;
const float B = 2.378405444e-04;
const float C = 2.019202697e-07;

// Variables para almacenar los resultados
int tempEnteroAM = 0, tempDecimalAM = 0;
int tempEnteroNTC = 0, tempDecimalNTC = 0;

// Variables para promedios
float sumaAM = 0.0, sumaNTC = 0.0;

// Número de muestras
const int numMuestras = 10;
// Prototipos de funciones para el display de 7 segmentos
void condicion_start(void);
void condicion_stop (void);
void send_byte (char data);
void send_data (int number);

// Función para leer el sensor AM2320
bool leerAM2320(int &tempEntero, int &tempDecimal) 
{
    char data[8];
    // Paso 1: Despertar el sensor (enviar un comando nulo)
    char wakeup[1] = { 0x00 };
    i2c.write(AM2320_ADDRESS, wakeup, 1);
    ThisThread::sleep_for(1ms);  // Pausa corta para asegurar la comunicación

    // Paso 2: Enviar el comando de lectura (función 0x03 para leer registros)
    data[0] = 0x03;  // Código de función para lectura
    data[1] = 0x00;  // Dirección del registro de temperatura
    data[2] = 0x04;  // Leer 4 registros (2 para temperatura)

    // Enviar el comando de lectura
    if (i2c.write(AM2320_ADDRESS, data, 3) != 0) {
        return false;  // Error en la escritura
    }

    // Esperar la respuesta
    ThisThread::sleep_for(2ms);

    // Paso 3: Leer los datos del sensor
    if (i2c.read(AM2320_ADDRESS, data, 8) != 0) {
        return false;  // Error en la lectura
    }

    // Extracción de la temperatura de los bytes recibidos
    int temperature_raw = (data[4] << 8) | data[5];  // Temperatura (2 bytes)

    // Separar enteros y decimales para la temperatura
    int temp_raw_corrected = temperature_raw - 00;  // Ajuste de -5 grados
    tempEntero = temp_raw_corrected / 10;
    tempDecimal = temp_raw_corrected % 10;

    return true;
}
// Función para calcular la resistencia del termistor basado en el voltaje medido
float calcularResistencia(float Vout) 
{
    return Rfija * ((Vin / Vout) - 1);
}

// Función para calcular la temperatura usando la ecuación de Steinhart-Hart
float calcularTemperatura(float R) 
{
    float logR = log(R);
    float temperaturaKelvin = 1.0 / (A + (B * logR) + (C * logR * logR * logR));
    return temperaturaKelvin - 273.15;  // Convertir de Kelvin a Celsius
}

// Función para leer la temperatura del NTC (termistor)
void leerNTC(int &tempEntero, int &tempDecimal) 
{
    float Vout = ntc_sensor.read() * Vin;  // Leer la tensión en el divisor de voltaje
    float Rntc = calcularResistencia(Vout);  // Calcular la resistencia del NTC

    // Calcular la temperatura en grados Celsius
    float tempC = calcularTemperatura(Rntc);

    // Separar la parte entera y decimal para la impresión
    tempEntero = static_cast<int>(tempC);
    tempDecimal = static_cast<int>((tempC - tempEntero) * 100);  // Multiplicar por 100 para obtener dos decimales
}

// Función para actualizar la pantalla OLED con los datos
void actualizarPantalla(int tempEnteroAM, int tempDecimalAM, int tempEnteroNTC, int tempDecimalNTC) 
{
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextCursor(0, 0);
    
    // Mostrar la temperatura del AM2320
    oled.printf("AM2320 Temp: %d.%02d C\n", tempEnteroAM, tempDecimalAM);
    // Mostrar la temperatura del NTC
    oled.printf("NTC Temp: %d.%02d C\n", tempEnteroNTC, tempDecimalNTC);
    
    oled.display();
}
// Función para calcular el error absoluto y relativo
void calcularErrores(float promedioAM, float promedioNTC) 
{
    float errorAbsoluto = fabs(promedioAM - promedioNTC);
    float errorRelativo = (errorAbsoluto / promedioAM) * 100;
    printf("Error Absoluto: %d.%02d C\n", static_cast<int>(errorAbsoluto), static_cast<int>((errorAbsoluto - static_cast<int>(errorAbsoluto)) * 100));
    printf("Error Relativo: %d.%02d %%\n", static_cast<int>(errorRelativo), static_cast<int>((errorRelativo - static_cast<int>(errorRelativo)) * 100));
    // Mostrar errores en el display de 7 segmentos
    send_data(static_cast<int>(errorAbsoluto * 100)); // Multiplicar por 100 para mostrar dos decimales
    ThisThread::sleep_for(2s); // Espera 2 segundos para ver el error absoluto
    send_data(static_cast<int>(errorRelativo * 100)); // Multiplicar por 100 para mostrar dos decimales
    ThisThread::sleep_for(2s); // Espera 2 segundos para ver el error relativo
}
// Función para inicializar y mostrar datos en el display de 7 segmentos
void send_data(int number) {
    condicion_start();
    send_byte(escritura);
    condicion_stop();
    condicion_start();
    send_byte(dir_display);

    int digits[4];

    for (int i = 0; i < 4; i++) {
        digits[3-i] = number % 10;

        number /= 10 ;
    }
     for (int i = 0; i < 4; i++){
        send_byte(digitToSegment[digits[i]]);
    }
    condicion_stop();
    condicion_start();
    send_byte(poner_brillo + 1);
    condicion_stop();
}
// Funciones auxiliares para la comunicación con el display de 7 segmentos
void condicion_start(void) {
    sclk = 1;
    dio.output();
    dio = 1;
    wait_us(1);
    dio = 0;
    sclk = 0;
}

void condicion_stop (void) {
    sclk = 0;
    dio.output();
    dio = 0;
    wait_us(1);
    sclk = 1;
    dio = 1;
}

void send_byte(char data) {
    dio.output();
    for (int i = 0; i < 8; i++) {
        sclk = 0;
        dio = (data & 0x01) ? 1 : 0;
        data >>= 1;
        sclk = 1;
    }
    dio.input();
    sclk = 0;
    wait_us(1);
    if (dio == 0) {
        sclk = 1;
        sclk = 0;
    }
}

int main() 
{
    // Inicializar la pantalla OLED
    oled.begin();
    oled.setTextSize(1);
    oled.setTextColor(1);
    oled.clearDisplay();
    oled.display();
    ThisThread::sleep_for(1000ms);

    while (true)  // Bucle infinito para repetir el proceso indefinidamente
    {
        sumaAM = 0.0;
        sumaNTC = 0.0;

        // Ciclo para realizar 10 lecturas de cada sensor
        for (int i = 0; i < numMuestras; i++) 
        {
            // Leer el sensor AM2320 y obtener los valores
            if (leerAM2320(tempEnteroAM, tempDecimalAM)) 
            {
                printf("AM2320 - Temperatura: %d.%02d C\n", tempEnteroAM, tempDecimalAM);
                sumaAM += (tempEnteroAM + tempDecimalAM / 100.0);
            } 
            else 
            {
                printf("Error al leer el sensor AM2320.\n");
            }

            // Leer la temperatura del NTC
            leerNTC(tempEnteroNTC, tempDecimalNTC);
            printf("NTC - Temperatura: %d.%02d C\n", tempEnteroNTC, tempDecimalNTC);
            sumaNTC += (tempEnteroNTC + tempDecimalNTC / 100.0);

            // Esperar antes de la próxima lectura
            ThisThread::sleep_for(1000ms);  // Esperar 1 segundo antes de la próxima iteración
        }

        // Calcular promedios después de las 10 lecturas
        float promedioAM = sumaAM / numMuestras;
        float promedioNTC = sumaNTC / numMuestras;

        // Separar la parte entera y decimal para el promedio
        int tempEnteroAMFinal = static_cast<int>(promedioAM);
        int tempDecimalAMFinal = static_cast<int>((promedioAM - tempEnteroAMFinal) * 100);

        int tempEnteroNTCFinal = static_cast<int>(promedioNTC);
        int tempDecimalNTCFinal = static_cast<int>((promedioNTC - tempEnteroNTCFinal) * 100);

        // Actualizar la pantalla OLED
        actualizarPantalla(tempEnteroAMFinal, tempDecimalAMFinal, tempEnteroNTCFinal, tempDecimalNTCFinal);

        // Mostrar resultados en consola
        printf("Promedio AM2320 - Temperatura: %d.%02d C\n", tempEnteroAMFinal, tempDecimalAMFinal);
        printf("Promedio NTC - Temperatura: %d.%02d C\n", tempEnteroNTCFinal, tempDecimalNTCFinal);

        // Calcular y mostrar errores
        calcularErrores(promedioAM, promedioNTC);

        // Esperar 5 segundos antes de repetir el proceso
        ThisThread::sleep_for(5000ms);
    }
}

