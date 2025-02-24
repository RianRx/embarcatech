#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "ssd1306.h"


#define ENDERECO_I2C 0x3C
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
ssd1306_t ssd;

// Definição dos pinos dos botões
#define BUTTON_1 5
#define BUTTON_2 6

// Definição dos pinos dos LEDs RGB
#define LED_R 11
#define LED_G 12
#define LED_B 13

// Flags para indicar eventos de botão
volatile bool button1_pressed = false;
volatile bool button2_pressed = false;

typedef enum {
    STATE_AUTH,         // Espera pela autenticação do usuário
    STATE_EXERCISE_START,  // Início do exercício
    STATE_IN_SERIES,    // Contando repetições e séries
    STATE_REST,         // Pausa entre as séries
    STATE_FINISH        // Finaliza o exercício
} State;

State current_state = STATE_AUTH; // Estado inicial
int repetitions_done = 0;         // Contador de repetições
int total_reps = 12;              // Número total de repetições por série
int current_series = 1;           // Contador de séries
int total_series = 3;             // Total de séries
bool is_user_authenticated = false; // Flag de autenticação do usuário
bool is_exercise_started = false;   // Flag de exercício iniciado
bool is_resting = false;            // Flag para o estado de descanso
bool is_finish_pressed = false;     // Flag para o botão de finalizar

// Estados do LED
typedef enum {
    LED_OFF,
    LED_ON,
    LED_BLINK
} led_state_t;

// Estrutura para controlar o LED
typedef struct {
    led_state_t state;
    uint8_t r, g, b;
    bool led_on;
} led_control_t;

volatile led_control_t led = {LED_OFF, 0, 0, 0, false};
static struct repeating_timer led_timer;

// Função de interrupção para os botões
void button_isr(uint gpio, uint32_t events) {
    if (gpio == BUTTON_1) {
        gpio_set_irq_enabled(BUTTON_1, GPIO_IRQ_EDGE_FALL, false);
        sleep_ms(200); // Debounce
        button1_pressed = true;
        gpio_set_irq_enabled(BUTTON_1, GPIO_IRQ_EDGE_FALL, true);
    } else if (gpio == BUTTON_2) {
        gpio_set_irq_enabled(BUTTON_2, GPIO_IRQ_EDGE_FALL, false);
        sleep_ms(200); // Debounce
        button2_pressed = true;
        gpio_set_irq_enabled(BUTTON_2, GPIO_IRQ_EDGE_FALL, true);
    } 
}

// Inicializa o display OLED
void init_display() {
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO_I2C, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false); // Limpa o display
}

// Configuração dos botões com interrupção
void setup_buttons() {
    gpio_init(BUTTON_1);
    gpio_set_dir(BUTTON_1, GPIO_IN);
    gpio_set_irq_enabled_with_callback(BUTTON_1, GPIO_IRQ_EDGE_FALL, true, &button_isr);

    gpio_init(BUTTON_2);
    gpio_set_dir(BUTTON_2, GPIO_IN);
    gpio_set_irq_enabled_with_callback(BUTTON_2, GPIO_IRQ_EDGE_FALL, true, &button_isr);
}

// Interrupção do timer para piscar o LED
bool blink_timer_callback(struct repeating_timer *t) {
    if (led.state == LED_BLINK) {
        led.led_on = !led.led_on; // Alterna entre ligado e desligado
        gpio_put(LED_R, led.led_on ? led.r : 0);
        gpio_put(LED_G, led.led_on ? led.g : 0);
        gpio_put(LED_B, led.led_on ? led.b : 0);
    }
    return true;
}

// Configuração dos LEDs
void setup_leds() {
    gpio_init(LED_R);
    gpio_set_dir(LED_R, GPIO_OUT);
    gpio_put(LED_R, 0);

    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_put(LED_G, 0);

    gpio_init(LED_B);
    gpio_set_dir(LED_B, GPIO_OUT);
    gpio_put(LED_B, 0);
}

// Configuração do LED RGB (Cor e Estado)
// Configuração do LED RGB (Cor e Estado)
void set_led(uint8_t r, uint8_t g, uint8_t b, led_state_t state) {
    led.r = r;
    led.g = g;
    led.b = b;
    led.state = state;

    if (state == LED_ON) {
        // Liga o LED na cor desejada
        gpio_put(LED_R, r);
        gpio_put(LED_G, g);
        gpio_put(LED_B, b);
        cancel_repeating_timer(&led_timer);
    } 
    else if (state == LED_OFF) {
        // Desliga o LED
        gpio_put(LED_R, 0);
        gpio_put(LED_G, 0);
        gpio_put(LED_B, 0);
        cancel_repeating_timer(&led_timer);
    } 
    else if (state == LED_BLINK) {
        // Ativa o piscar e define a cor
        start_led_blinking(500);  // Pisca a cada 500ms
    }
}

// Configuração do timer para piscar o LED
void start_led_blinking(uint32_t interval_ms) {
    cancel_repeating_timer(&led_timer);
    add_repeating_timer_ms(interval_ms, blink_timer_callback, NULL, &led_timer);
}

void display_message(ssd1306_t *ssd, const char* message1, const char* message2, const char* message3, const char* message4) {
    ssd1306_fill(ssd, false);  // Limpa o display

    ssd1306_draw_string(ssd, message1, 5, 5);  // Exibe a primeira linha
    ssd1306_draw_string(ssd, message2, 5, 20); // Exibe a segunda linha
    ssd1306_draw_string(ssd, message3, 5, 35);  // Exibe a terceira linha
    ssd1306_draw_string(ssd, message4, 5, 45);  // Exibe a quarta linha

    ssd1306_send_data(ssd);; // Atualiza o display para exibir as mudanças
}

// Funções auxiliares para transições de estado
void authenticate_user() {
    display_message(&ssd, "Aguardando", "autenticacao", "via QR CODE...", "");

    bool iot_message_received = false;  

    if (button1_pressed) {
        button1_pressed = false;
        is_user_authenticated = true; 
    }
}

void start_exercise() {
    display_message(&ssd, "Aguardando", "inicio", "do exercicio", "");
    if (button1_pressed) {
        button1_pressed = false;
        is_exercise_started = true; 
        current_series = 1;
    }
}

void in_series() {
    char tempo_str[20];    
    char repeticoes_str[20]; 
    char serie_str[20];    

    sprintf(tempo_str, "Tempo: %d", 5);
    sprintf(repeticoes_str, "Repeticoes: %d", repetitions_done);
    sprintf(serie_str, "Serie: %d", current_series);

    display_message(&ssd, tempo_str, repeticoes_str, serie_str, "");
    if (button2_pressed) {
        button2_pressed = false;
        repetitions_done++;
    }
}

void finish_exercise() {
    printf("Exercício finalizado! Gerando relatório...\n");
    is_finish_pressed = true;
}

void rest_time() {
    printf("Tempo de descanso iniciado...\n");
    is_resting = true;
}

void complete_series() {
    printf("Série %d completada!\n", current_series);
    current_series++;
    repetitions_done = 0;
}

void update_state() {
    switch(current_state) {
        case STATE_AUTH:
            if (is_user_authenticated) {
                // Transição para o próximo estado
                printf("Usuário autenticado!\n");
                current_state = STATE_EXERCISE_START;
            }
            break;
        
        case STATE_EXERCISE_START:
            if (is_exercise_started) {
                // Transição para o próximo estado
                printf("Exercício iniciado!\n");
                current_state = STATE_IN_SERIES;
                repetitions_done = 0;
            }
            break;
        
        case STATE_IN_SERIES:
            if (repetitions_done >= total_reps) {
                // Se as repetições de uma série forem completadas
                if (current_series < total_series) {
                    complete_series();
                    current_state = STATE_REST; // Entrar no estado de descanso
                } else {
                    current_state = STATE_FINISH; // Finalizar o exercício
                }
            }
            break;
        
        case STATE_REST:
            // Simula o tempo de descanso
            rest_time();
            // Após o tempo de descanso, voltar para a contagem de repetições
            current_state = STATE_IN_SERIES;
            break;
        
        case STATE_FINISH:
            if (is_finish_pressed) {
                finish_exercise();
                // Enviar dados para a nuvem ou gerar relatório
                current_state = STATE_AUTH;  // Retornar ao estado inicial
            }
            break;

        default:
            printf("Estado inválido!\n");
            break;
    }
}

int main() {
    stdio_init_all();
    init_display();
    setup_buttons();
    setup_leds();
  
    while (1) {
     // Simula a autenticação
        if (current_state == STATE_AUTH) {
            authenticate_user();
        }

        // Simula o início do exercício
        if (current_state == STATE_EXERCISE_START) {
            start_exercise();
        }

        // Simula a execução do exercício
        if (current_state == STATE_IN_SERIES) {
            in_series();
        }

        // Simula o descanso
        if (current_state == STATE_REST) {
            printf("Descanso entre séries\n");
        }

        // Simula a finalização do exercício
        if (current_state == STATE_FINISH) {
            finish_exercise();
        }

        // Atualiza o estado da máquina
        update_state();
        
        // Simula um atraso de 1 segundo
        sleep_ms(1000);
    }
}

