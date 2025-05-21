#include <stdio.h>               
#include <string.h>              
#include <stdlib.h>              

#include "pico/stdlib.h"         
#include "hardware/adc.h"        
#include "pico/cyw43_arch.h"      

#include "lwip/pbuf.h"           // Manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Funções e estruturas para trabalhar com interfaces de rede (netif)

#include "hardware/clocks.h"
#include "pico/bootrom.h"
#include "hardware/i2c.h"
#include "./lib/ssd1306.h"
#include "./lib/font.h"
#include "hardware/pio.h"
#include "Webserver_01.pio.h"
#include <math.h>
//---------------------------------------------------
// DEFINES
//---------------------------------------------------
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE 12                
#define LED_GREEN 11               
#define LED_RED 13                 
#define botaoB 6
#define BUTTON_A 5
#define BUZZER 21
#define I2C_PORT        i2c1
#define I2C_SDA         14
#define I2C_SCL         15
#define ENDERECO        0x3C
#define LED_MATRIX      7
#define TOTAL_LEDS      25
#define MAX_LED_VALUE   60

//---------------------------------------------------
// VARIAVEIS GLOBAIS
//---------------------------------------------------
float temperatura = 20;
int32_t cortina = 0;
bool flag_lampada = false;
ssd1306_t ssd; // Display de 128 x 64
char string_cortina[5];
char string_ar[5];

//---------------------------------------------------
// PROTOTIPAÇÃO
//---------------------------------------------------
void init_pins(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
// Tratamento do request do usuário
void user_request(char **request);
// Interrupção para colocar placa no modo bootsel
void gpio_irq_handler(uint gpio, uint32_t events);
//Controle do display
void AtualizaDisplay(void);
//Controle Matriz
void controle_matrix(PIO pio, uint sm, int cortina);

// Função principal
int main()
{
    //Inicialização padrão
    stdio_init_all();
    // Inicializar os Pinos GPIO e demais perifericos utilizados
    init_pins();
    PIO pio = pio0;
    bool clk = set_sys_clock_khz(128000, false);
    uint offset = pio_add_program(pio, &Webserver_01_program);
    uint sm_pio = pio_claim_unused_sm(pio, true);
    Webserver_01_program_init(pio, sm_pio, offset, LED_MATRIX);


    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 50000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");


    while (true)
    {
        /* 
        * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
        * Este método deve ser chamado periodicamente a partir do ciclo principal 
        * quando se utiliza um estilo de sondagem pico_cyw43_arch 
        */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        AtualizaDisplay();
        controle_matrix(pio, sm_pio, cortina);
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void init_pins(void){
    // Configuração dos LEDs como saída
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, false);
    
    gpio_init(LED_GREEN);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, false);
    
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, false);

    // Para ser utilizado o modo BOOTSEL com botão B
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Configura I2C para o display
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    i2c_init(I2C_PORT, 400000); // 0.4 MHz
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request){

    if (strstr(*request, "GET /lampada_on") != NULL)
    {
        gpio_put(LED_BLUE, 1);
    }else if (strstr(*request, "GET /lampada_off") != NULL){
        gpio_put(LED_BLUE, 0);
    }else if (strstr(*request, "GET /cortina_on") != NULL){
        cortina += 10;
    }else if (strstr(*request, "GET /cortina_off") != NULL){
        cortina -= 10;
    }else if (strstr(*request, "GET /temp_mais") != NULL){
        temperatura = temperatura + 1;
    }else if (strstr(*request, "GET /temp_menos") != NULL){
        temperatura = temperatura - 1;
    }
    

};

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    // Cria a resposta HTML
    char html[1500];

    // Instruções html do webserver
    snprintf(html, sizeof(html),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<title>Smarth House</title>\n"
    "<style>\n"
    "body { background-color: #e0f7fa; font-family: Calibri, sans-serif; text-align: center; margin-top: 50px; }\n"
    "h1 { font-size: 64px; margin-bottom: 30px; }\n"
    "button { background-color: LightGreen; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px; width: 80%%; max-width: 400px; }\n"
    ".temperature { font-size: 48px; margin-top: 30px; color: #333; }\n"
    ".linha { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; }\n"
    ".linha form { margin: 0; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>Smarth House: Painel de Controle</h1>\n"

    // Botões da lâmpada (na mesma linha)
    "<div class=\"linha\">\n"
    "<form action=\"./lampada_on\"><button>Ligar Lâmpada</button></form>\n"
    "<form action=\"./lampada_off\"><button>Desligar Lâmpada</button></form>\n"
    "</div>\n"
    "<div class=\"linha\">\n"
    "<form action=\"./cortina_on\"><button>Abrir Cortina</button></form>\n"
    "<form action=\"./cortina_off\"><button>Fechar Cortina</button></form>\n"
    "</div>\n"
        // Temperatura
    "<p class=\"temperature\">Abertura da cortina: %d %</p>\n"
    "<div class=\"linha\">\n"
    "<form action=\"./temp_mais\"><button>Aumentar Temperatura</button></form>\n"
    "<form action=\"./temp_menos\"><button>Diminuir Temperatura</button></form>\n"
    "</div>\n"
    // Temperatura
    "<p class=\"temperature\">Temperatura do Ar: %.1f &deg;C </p>\n"

    "</body>\n"
    "</html>\n",
    cortina,temperatura);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}
void gpio_irq_handler(uint gpio, uint32_t events)
{
    static absolute_time_t last_time_A = 0;
    static absolute_time_t last_time_B = 0;
    absolute_time_t now = get_absolute_time();

    if (gpio == BUTTON_A) {
        if (absolute_time_diff_us(last_time_A, now) > 200000) {

            last_time_A = now;
        }
    } else if (gpio == botaoB) {
        if (absolute_time_diff_us(last_time_B, now) > 200000) {
            reset_usb_boot(0, 0);
            last_time_B = now;
        }

}}
void AtualizaDisplay(void){
    sprintf(string_cortina,"%d", cortina);
    snprintf(string_ar, sizeof(string_ar), "%.2f%%", temperatura);
    ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, 1, 1, 126, 62, 1, 1); // Borda grossa
    ssd1306_rect(&ssd, 4, 4, 120, 56, 0, 1); // Borda grossa
    ssd1306_draw_string(&ssd, "LAMPADA ", 5, 7);
    if (flag_lampada){
     ssd1306_draw_string(&ssd, "ON", 70, 7);   
    }else{ssd1306_draw_string(&ssd, "OFF", 70, 7); }
    ssd1306_draw_string(&ssd, "CORTINA :", 5, 17);
    ssd1306_draw_string(&ssd, string_cortina, 70, 17);
    ssd1306_draw_string(&ssd, "%", 100, 17);
    ssd1306_draw_string(&ssd, "TEMP. :", 5, 27);
    ssd1306_draw_string(&ssd, string_ar, 80, 27);
    ssd1306_draw_string(&ssd, " C", 100, 27);
    ssd1306_send_data(&ssd);
}

void controle_matrix(PIO pio, uint sm, int cortina){
    int8_t cor_steps = 8;
    // Mapeia volume [0, volume_max] para [0, TOTAL_LEDS] (valor fracionário)
    double leds_ativos = (TOTAL_LEDS * cortina) / 100;
    int leds_full = (int)leds_ativos;
    double led_parcial = fmod(leds_ativos, 1.0);
    int brilho_parcial = (int)(led_parcial * MAX_LED_VALUE);
    
    uint32_t valor_full = ((uint32_t)MAX_LED_VALUE) << cor_steps;
    uint32_t valor_parcial = ((uint32_t)brilho_parcial) << cor_steps;
    
    uint32_t valor[TOTAL_LEDS];
    for (int i = 0; i < TOTAL_LEDS; i++) {
        if (i < leds_full) {
            valor[i] = valor_full;
        } else if (i == leds_full && (led_parcial > 0.0)) {
            valor[i] = valor_parcial;
        } else {
            valor[i] = 0;
        }
    }
    for (int i = 0; i < TOTAL_LEDS; i++) {
        pio_sm_put_blocking(pio, sm, valor[i]);
    }
}