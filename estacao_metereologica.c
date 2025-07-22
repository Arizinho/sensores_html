#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"
#include "aht20.h"
#include "bmp280.h"
#include <math.h>
#include "hardware/clocks.h"

//arquivo .pio
#include "led_matrix.pio.h"

// I2C para sensores e display
#define I2C_PORT i2c0               // i2c0 pinos 0 e 1, i2c1 pinos 2 e 3
#define I2C_SDA 0                   // 0 ou 2
#define I2C_SCL 1                   // 1 ou 3
#define SEA_LEVEL_PRESSURE 101325.0 // Pressão ao nível do mar em Pa

#define I2C_PORT_DISP i2c1          // Porta I2C1 para display
#define I2C_SDA_DISP 14             // SDA do display no GPIO14
#define I2C_SCL_DISP 15             // SCL do display no GPIO15
#define endereco 0x3C               // Endereço I2C do SSD1306

#define LED_RED 13 //GPIO led vermelho
#define LED_GREEN 11 //GPIO led verde
#define BUZZER 10 //GPIO buzzer
#define OUT_PIN 7 //GPIO matriz de leds
#define BOTAO_A 5 //GPIO botão A

//Definições do Wi-Fi
#define WIFI_SSID "WIFI_NAME"
#define WIFI_PASS "WIFI_PASSWORD"

//Variáveis globais
AHT20_Data data; //Estrutura para armazenar os dados do AHT20
double pressure = 0.0; //Variável para armazenar a pressão atmosférica
struct limits_values {
    float min_temp;
    float max_temp;
    float min_hum;
    float max_hum;
} limits = {
    .min_temp = 0.0,
    .max_temp = 50.0,
    .min_hum = 0.0,
    .max_hum = 100.0
}; //Limites de temperatura e umidade
bool out_of_limits = false; //Indica se os valores estão fora dos limites
bool mode = 0; //Indoca modo de operação do display

//Protótipos de funções
uint32_t matrix_rgb(uint8_t b, uint8_t r, uint8_t g);
void desenho_pio(bool modo, PIO pio, uint sm);

//HTML para o servidor web
const char HTML_BODY[] =
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'><title>Estação Meteorológica</title>"
"<style>\n"
"body { font-family: sans-serif; text-align: center; padding: 10px; margin: 0; background: #f9f9f9; }\n"
" .graficos { width: 600px; height: 300px; margin: 20px auto; }\n"
"</style>\n"
"<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>\n"
"</head>\n"
"<body>\n"
"<h1>Estação Meteorológica</h1>\n"
"<p>Temperatura: <span id='temperatura'>--</span></p>\n"
"<p>Pressão: <span id='pressure'>--</span></p>\n"
"<p>Umidade: <span id='umidade'>--</span></p>\n"
"<canvas id=\"grafico\" class=\"graficos\"></canvas>\n"
"<br><h3>Configuração de Limites</h3>\n"
"<table style='margin: 0 auto; border: 2px solid #333; border-collapse: collapse;'>"
"<tr>"
"<td style='border-right: 1px solid #333; padding: 6px;'>Temperatura Mínima (valor atual: <span id='actualtempMin'>--</span>): <input type='number' id='tempMin' value='0'></td>\n"
"<td>Umidade Mínima (valor atual: <span id='actualhumMin'>--</span>): <input type='number' id='humMin' value='0'></td></tr>\n"
"<tr><td style='border-right: 1px solid #333; padding: 6px;'>Temperatura Máxima (valor atual: <span id='actualtempMax'>--</span>): <input type='number' id='tempMax' value='50'></td>\n"
"<td>Umidade Máxima (valor atual: <span id='actualhumMax'>--</span>): <input type='number' id='humMax' value='100'></td></tr></table>\n"
"<br><button onclick='sendConfig()'>Enviar Configuração</button>\n"
"<div id=\"alerta\" style=\"color:red; font-weight:bold; margin-top:10px;\"></div>"
"<script>\n"
"function sendConfig() {\n"
"const tempMin = document.getElementById('tempMin').value;\n"
"const tempMax = document.getElementById('tempMax').value;\n"
"const humMin = document.getElementById('humMin').value;\n"
"const humMax = document.getElementById('humMax').value;\n"
"fetch(`/config?tempMin=${tempMin}&tempMax=${tempMax}&humMin=${humMin}&humMax=${humMax}`);\n"
"}\n"
"function atualizar() {\n"
"fetch('/estado')\n"
".then(res => res.json())\n"
".then(data => {\n"
"document.getElementById('temperatura').innerText = data.temperatura + ' °C';\n"
"document.getElementById('pressure').innerText = data.pressure + ' kPa';\n"
"document.getElementById('umidade').innerText = data.umidade + '%';\n"
"document.getElementById('actualtempMin').innerText = data.min_temp + ' °C';\n"
"document.getElementById('actualtempMax').innerText = data.max_temp + ' °C';\n"
"document.getElementById('actualhumMin').innerText = data.min_hum + ' %';\n"
"document.getElementById('actualhumMax').innerText = data.max_hum + ' %';\n"
"document.getElementById('alerta').innerText = data.alerta ? \"ALERTA: Valores fora dos limites!\" : \"\";"
"});\n"
"}\n"
"const chart = new Chart(document.getElementById('grafico').getContext('2d'), {\n"
"type: 'line',\n"
"data: {\n"
"labels: [],\n"
"datasets: [\n"
"{ label: 'Temperatura (°C)', data: [], borderColor: 'red', fill: false },\n"
"{ label: 'Pressão (kPa)', data: [], borderColor: 'green', fill: false },\n"
"{ label: 'Umidade (%)', data: [], borderColor: 'blue', fill: false }\n"
"]\n"
"},\n"
"options: {\n"
"responsive: false,\n"
"maintainAspectRatio: false,\n"
"animation: false,\n"
"scales: {\n"
"x: {\n"
"display: true,\n"
"ticks: { display: false }\n"
"},\n"
"y: {\n"
"beginAtZero: true\n"
"}\n"
"}\n"
"}\n"
"});\n"
"setInterval(atualizar, 1000);\n"
"setInterval(async () => {\n"
"const data = await fetch('/estado').then(res => res.json());\n"
"chart.data.labels.push('');\n"
"chart.data.datasets[0].data.push(data.temperatura);\n"
"chart.data.datasets[1].data.push(data.pressure);\n"
"chart.data.datasets[2].data.push(data.umidade);\n"
"if (chart.data.datasets[0].data.length > 10) {\n"
"chart.data.labels.shift();\n"
"chart.data.datasets[0].data.shift();\n"
"chart.data.datasets[1].data.shift();\n"
"chart.data.datasets[2].data.shift();\n"
"}\n"
"chart.update();\n"
"}, 3000);\n"
"</script>\n"
"</body>\n"
"</html>\n";

// Função para calcular a altitude a partir da pressão atmosférica
double calculate_altitude(double pressure)
{
    return 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
}

// Estrutura do estado HTTP
struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

// Callback ao enviar resposta HTTP
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

// Callback ao receber requisição HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /config")) {
        // Atualiza limites recebidos pela URL
        float tempMin = limits.min_temp, tempMax = limits.max_temp, humMin = limits.min_hum, humMax = limits.max_hum;
        char *q = strstr(req, "GET /config?");
        if (q) {
            char *params = q + strlen("GET /config?");
            sscanf(params, "tempMin=%f&tempMax=%f&humMin=%f&humMax=%f", &tempMin, &tempMax, &humMin, &humMax);
            limits.min_temp = tempMin;
            limits.max_temp = tempMax;
            limits.min_hum = humMin;
            limits.max_hum = humMax;
            printf("Configuração recebida: tempMin=%.1f, tempMax=%.1f, humMin=%.1f, humMax=%.1f\n", tempMin, tempMax, humMin, humMax);
        }
        hs->len = snprintf(hs->response, sizeof(hs->response),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: 2\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "OK");
    }
    else if (strstr(req, "GET /estado"))
    {        
        // Envia JSON com estado dos sensores
        char json_payload[192];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                    "{\"temperatura\":%.1f,\"pressure\":%.1f,\"umidade\":%.1f,\"min_temp\":%.1f,\"max_temp\":%.1f,\"min_hum\":%.1f,\"max_hum\":%.1f,\"alerta\":%s}\r\n",
                    data.temperature, pressure/1000.0, data.humidity,
                    limits.min_temp, limits.max_temp, limits.min_hum, limits.max_hum,
                    out_of_limits ? "true" : "false");

        printf("[DEBUG] JSON: %s\n", json_payload);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    else
    {
        // Página principal HTML
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

// Callback ao aceitar nova conexão TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

void gpio_irq_handler(uint gpio, uint32_t events)
{
    static uint32_t last_time = 0;
    // Verifica se o tempo desde a última interrupção é maior que 200 ms
    if (to_ms_since_boot(get_absolute_time()) - last_time > 200)
    {
        last_time = to_ms_since_boot(get_absolute_time()); // Atualiza o tempo da última interrupção
        mode = !mode; // Alterna o modo

    }
}

int main()
{
    stdio_init_all();

    // Inicializa GPIOs
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, 0); // Desliga o LED vermelho inicialmente

    // Inicializa PWM no buzzer
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_set_clkdiv(5, 125.0); 
    pwm_set_wrap(5, 1999); 
    pwm_set_gpio_level(BUZZER, 0); 
    pwm_set_enabled(5, true); 

    // Inicialização da matriz de LEDs
    PIO pio = pio0; //seleciona a pio0
    uint offset = pio_add_program(pio, &pio_matrix_program);
    uint sm = pio_claim_unused_sm(pio, true);
    pio_matrix_program_init(pio, sm, offset, OUT_PIN);

    // Inicializa I2C para sensores
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa o BMP280
    bmp280_init(I2C_PORT);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT, &params);

    // Inicializa o AHT20
    aht20_reset(I2C_PORT);
    aht20_init(I2C_PORT);

    // Inicializa I2C para display SSD1306
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    // Inicializa display SSD1306
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    // Mostra status Wi-Fi
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);    
    ssd1306_send_data(&ssd);

    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Mostra IP no display
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    // Inicia servidor HTTP
    start_http_server();

    char str_alt[5];
    char str_tmp[5];
    char str_umi[5];
    char str_press[5];  
    bool cor = true;
    bool wifi_connected = false;
    uint8_t contador = 0;

    while (true)
    {
        cyw43_arch_poll(); // Processa eventos da pilha TCP/IP

        sleep_ms(20);

        if (contador >= 25) { 
            contador = 0;

            // Verifica se valores estão fora dos limites
            if (data.temperature < limits.min_temp || data.temperature > limits.max_temp ||
                data.humidity < limits.min_hum || data.humidity > limits.max_hum) 
            {
                out_of_limits = true;
            }
            else {
                out_of_limits = false;
            }
            
            // Acende LED vermelho se fora dos limites
            gpio_put(LED_RED, out_of_limits); 
            // Acende LED verde se dentro dos limites
            gpio_put(LED_GREEN, !out_of_limits); 

            // Aciona buzzer para alerta
            out_of_limits ? pwm_set_gpio_level(BUZZER, 1000) : pwm_set_gpio_level(BUZZER, 0);

            // Liga alerta na matriz de LEDs através do booleano
            desenho_pio(out_of_limits, pio, sm);

            // Leitura do BMP280
            int32_t raw_temp_bmp;
            int32_t raw_pressure;
            bmp280_read_raw(I2C_PORT, &raw_temp_bmp, &raw_pressure);
            pressure = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &params);

            // Cálculo da altitude
            double altitude = calculate_altitude(pressure);

            // Leitura do AHT20
            aht20_read(I2C_PORT, &data);

            // Verifica conexão Wi-Fi
            wifi_connected = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;

            // Converte valores para string
            sprintf(str_press, "%.0fkPa", pressure / 1000.0);      
            sprintf(str_alt, "%.0fm", altitude);
            sprintf(str_tmp, "%.1fC", data.temperature);
            sprintf(str_umi, "%.1f%%", data.humidity);
        
            if (!mode) {
                // Atualiza display com dados
                ssd1306_fill(&ssd, !cor);                           
                ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);       
                ssd1306_line(&ssd, 3, 14, 123, 14, cor);            
                ssd1306_line(&ssd, 3, 25, 123, 25, cor);           
                ssd1306_line(&ssd, 3, 25, 123, 25, cor);           
                ssd1306_line(&ssd, 3, 37, 123, 37, cor);           
                ssd1306_draw_string(&ssd, ip_str, 10, 5);         
                out_of_limits ? ssd1306_draw_string(&ssd, "ALERTA!!!", 30, 16) : ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16); 
                out_of_limits ? ssd1306_draw_string(&ssd, "Limit excedido", 9, 28) : ssd1306_draw_string(&ssd, "Dados Sensores", 9, 28);
                ssd1306_line(&ssd, 63, 38, 63, 61, cor);            
                ssd1306_draw_string(&ssd, str_press, 14, 41);            
                ssd1306_draw_string(&ssd, str_alt, 14, 52);             
                ssd1306_draw_string(&ssd, str_tmp, 73, 41);             
                ssd1306_draw_string(&ssd, str_umi, 73, 52);            
                ssd1306_send_data(&ssd); 
            } else {
                // Modo 1: mostra IP e status Wi-Fi
                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Info Wi-Fi", 0, 0);
                ssd1306_draw_string(&ssd, "IP:", 0, 15);
                ssd1306_draw_string(&ssd, ip_str, 30, 15);
                wifi_connected ? ssd1306_draw_string(&ssd, "Wi-Fi: CONECTADO", 0, 30) : ssd1306_draw_string(&ssd, "Wi-Fi: DESCONECTADO", 0, 30);
                ssd1306_send_data(&ssd);
            }           
        }
        contador++;
    }

    cyw43_arch_deinit();
    return 0;
}

//rotina para definição da intensidade de cores do led na matriz 5x5
uint32_t matrix_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return (g << 24) | (r << 16) | (b << 8);
}

//rotina para acionar a matrix de leds - ws2812b
void desenho_pio(bool modo, PIO pio, uint sm){
    for (int16_t i = 0; i < 25; i++) {
        if (modo){
            (i%5 == 2 && i != 7) ? pio_sm_put_blocking(pio, sm, matrix_rgb(5, 5, 5)) : pio_sm_put_blocking(pio, sm, matrix_rgb(15, 0, 0));
        }
        else {
            pio_sm_put_blocking(pio, sm, matrix_rgb(0, 0, 0));
        }
    }
}