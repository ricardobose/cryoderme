/*
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

/*
 * Com base no codigo do proojeto Display+STM teremos o inicio do
 * código do Cryoderme. Logo abaixo teremos o código Display+STM agora com textos coerentes a cada estado. 
 * v1 - organiza de forma geral as funções e variáveis para display em cada estado da State Machine
 * 		tambem foi organizada uma funação para mostrar texto de forma corrida. Isso é para caracteres grandes apenas.
 * v2 - Incluir timer para ser usado na medição das temperaturas.
 * v3 - Incluir leitura de sensores de temperatura. Ao invés de fazer uma interrupção por timer optei em fazer uma thread 
 * 		que dorme com uma função SLEEP a cada "SLEEP_TEMPERATURA_MS" milisegundos, e qdo acorda entra no schedulling para fazer a leitura dos ADCs 
 * 		(temperaturas), que após ler os valores e colocalos em variáveis adequadas entra em SLEEP novamente. Assim ficamos 
 * 		trabalhando com o mínimo de interrupção e o máximo de schedulling do kernel.
 * 		Além disso, a temperaura de saída do bloco Frio2 é apresentado no display.   
 * v4 - Incorpora comportamento ao ser conectado na energia, o firmware entra no STM de Desliagr e com isso desliaga todas 
 * 		saidas e mostrar OFF no display.  
 * 		Ainda incorpora comportamento sobre o botão Liga/Desliga com interrupção no botão BT_LIGA_DESL. Um toque o equipamento liga
 * 		e mantendo ao menos XX segundos pressionado o equipamento desliga.   
 * v5 - incorpora as entradas e saídas digitais necessário ao controle do equipammento. Segue lista no item PINOUT
 *    
 * 
 * ESTADOS do State Machine
 *  - ST_BT_Inicio: Ao press botão de início-> liga a bomba de agua, ar, leds, as célualr e msg display 
 *  - ST_BT_Fim: Ao manter press botão inicio-> faz o oposto do liga 
 *  - ST_Alarms: Gera mensagens de alarmes e liga dreno,   
 *  - ST_BT_Temp: Ao press botão TEMP, opçao "Ciclo T=-35 oC" liga todas células e display msg 
 * 									se "Ciclo T= 0 oC", liga parte das células e dislay msg 
 *  - ST_BT_Flow: Ao press botão FLOW, opção "Ciclo Alto Fluxo" muda freq aimentação Bomba para 60Hz e display msg 
 * 								 opção "Ciclo Baixo Fluxo" muda freq aimentação Bomba para 45Hz e display msg
 *  - ST_Temp: Gera mensagem de temperatura da saída de ar 
 *  - ST_Idle: Não faz nada.  
 * 
 * DETALHES GERAIS
 * - Os sensores de tempratura são lidos na interrupção de um timer a cada 'SENS_PERIOD_S' segundos. Na interrupção já é verificado se
 * há alguma valor alterado e acionado estado de alarmes. 
 * - Estado "FIM" do State Machine desliga todas as saídas e coloca a cpu em modo SLEEP. A fonte de alimentação não é desligada. 
 * - O Estado "FIM" é acionado pressionando o botão liga por mais de 3 segundos
 * - Sempre que o equipamentos é ligado na energia o processador inicia e vai para o modo "FIM" do state machine.
 * - Mesmo em modo desligado a CPU nãoserá desligada. Apenas as saídas e display, garntindo que o equipamento não está em funcionamento.
 *   Ao toqueno botão de Ligar o equipamento liga os ementos de fresfriamento, bombas, etc..    
 * - 
 * 
 * 
 * 
 *  -- PINOUT I2C
 *  PB6 - i2c1 scl 	- Display
 *  PB9 - i2c  sda 	- Display
 *  -- PINOUT Entradas ADC
 *  PA0 - ADC_IN1 (channel 1)	- Temp Bloco Quente
 *  PA1 - ADC_IN2 (channel 2) 	- Temp Bloco Frio 1º estágio
 * 	PC0 - ADC_IN6 (channel 6)	- Temp Bloco Frio 2º estágio (mais frio)
 *  PC1 - ADC_IN7 (channel 7)	- Temp Bloco Ambiente (a decidir)
 *  -- PINOUT ENTRADAS DIGITAIS 
 *  PA9 - Botao BT_LIG_DESL  	- usado para ligar e desligar equipamento 
 *  PB10 - Botao BT_FLOW		- usado para controlar fluxo de ar  
 *  PA8 - Botao BT_TEMP			- usado para controlar temperatura de saída 
 *  -- PINOUT SAIDAS DIGITAIS 
 *  PB13 - OUT LED_LIG_DESL  	-  Usado para acionar led do botao Liga/desli
 *  PB14 - OUT LED_FLOW      	-  Usado para acionar led do botão FLow 
 *  PB15 - OUT LED_TEMP   	 	-  Usado para acionar led do botao Temperatura (LED_TEMP)
 *  PB5  - OUT AIR_1   			-  Usado para acionar bomba de ar, 60hz  (OUT_AIR_1) 
 *  PB4  - OUT AIR_2   		 	-  Usado para acionar bomba de ar, 45hz  (OUT_AIR_2)
 *  PB2  - OUT BANK_PELT_1   	-  Usado para aciona DC-DC para células Peltier 12706 (6 unidades) + bomba agua, valvula sol, coolers
 *  PB1  - OUT BANK_PELT_2   	-  Usado para aciona DC-DC para células Peltier 12715 (4 unidades)
 *  
 *  
 * 
 *  COMANDO   west build -b NUCLEO_F303RE -p -- -DSHIELD=sh1106_128x64
 *            west flasg
 * 
 */



#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/display/cfb.h>
#include <zephyr/smf.h>			//State machine
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/__assert.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "cfb_font_fixedsys32.h"
#include "cfb_font_jetbrains_mono_bold.h"




/*  Define do projeto*/
#define STACKSIZE 1024		/* size of stack area used by each thread */
#define PRIORITY 7			/* scheduling priority used by each thread */
#define SLEEP_TIME_MS   1000	/* 1000 msec = 1 sec */
#define SENS_PERIOD_S  2	/* Periodo leitura sensores temperaturas, em segundos */
#define TEXT_PERIOD_S  150	/* Periodo controle texto corrido, em milisegundos */
#define SLEEP_TEMPERATURA_MS  5000	/* Periodo para leitura de sensores de temperatura*/
#define BUTTON_OFF_TIME	3000	/* Periodo que botão Desligar deve ficar pressionado para entrar no modo Desligar Equipamento*/	
#define STACKSIZE 1024 		/* size of stack area used by each thread */
#define PRIORITY 7			/* scheduling priority used by each thread */
#define LED0_NODE DT_ALIAS(led0)
#define BUTTON0_NODE DT_NODELABEL(user_button)

/*** Variáveis Globais ****/
const struct device *dev;
void display_corrido (char*, int);	// função no fim do texto, executa msg de forma corrida no display
bool low_temp_trabalho=true;		// variavel true indica low temperaturade trabalho (-35ºC), e false para alta temperatura (0 ªC)
bool high_flow_trabalho=true;		// variavel true indica Hi fluxo de ar, e false indica baixo fluxo de ar 
bool sistem_ON=false;			  	// usado para informar se o equipammento está ligado ou desligado. Embora a CPU esteja sempre ligada.
int ret;						// variável de retorno de funções nativas do Zephyr, exemplo: verificação se saída foi configurada corretamente
static const struct smf_state demo_states[]; /* Forward declaration of state table */
enum demo_state { ST_BT_Inicio, ST_BT_Fim, ST_Alarms, ST_BT_Temp, ST_BT_Flow, ST_Temp, ST_Idle}; /* List of demo states, ver cabeçalho */
int alarmes;					// variável global para controle dos alarmes, em conjunto com erros_alarmes
enum erros_alarmes{ERR_NONE, ERR_COOLER1, ERR_COOLER2, ERR_WARMPART};	// 0 - sem notificação, 1 - Erro primeiro estágio de resfriamento, 2 -  Erro primeiro estágio de resfriamento, 3 Erro estágio de arrefecimento 
struct s_object {	/* User defined object */
    struct smf_ctx ctx;	/* This must be first */
	} s_obj;
int count_tempo_dreno=0;
int count=0;	// usado em teste
float TempSensor_frio1=0.0, TempSensor_frio2=0.0, TempSensor_quente=0.0; 
void temp_sensors(void);	// declaração de função de leitura de ADCs
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);			// declara LED conforme arquivo .dts do Board nucleo_f303re
const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON0_NODE, gpios);	// declara LED conforme arquivo .dts do Board nucleo_f303re
static struct gpio_callback button_cb_data;									// declara struc usada para callback (interrupção) do botão	
void inicializa_GPIO_Interrupcao(void);
void work_handler(struct k_work *work);
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);


/************************/
// RELATIVO AO DISPLAY E ESTADOS DA STATE MACHINES
/************************/
/* State ST_Inicio:   */
/* Início é quando o botão Iniciar é acionado. No estado desligado as fontes estão ligadas mas todo o resto fica desligado  
 * Então uma vez acionado o botão este estado liga as tudo e passa a funcionar normalmente. 
 * O Botão de lIga deverá ser um botão no terminal de interrupão externa capáz de tirar a cpu do modo SLEEp 
*/
static void st_bt_inicio_run(void *o)  { 
	
	 	display_blanking_off(dev);
		cfb_framebuffer_clear(dev, false);
		cfb_framebuffer_set_font(dev, 0);
		cfb_print(dev, " ON ", 0, 0);
		cfb_framebuffer_set_font(dev, 1);
		cfb_print(dev, "CRYODERME V 1", 8, 48);	// posição em multiplos 8 para y e x
		cfb_framebuffer_finalize(dev);
		
		// liga celulas, bombas e LEDs, 
		// ...
		k_msleep(2000);
	smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Idle]);	//set STM para voltar a Idle State depois de finalizar este estado
	}

/* State ST_Fim - Desliga tudo que Inicia ligou, e coloca o processador em SLEEP. */
static void st_bt_fim_run(void *o){
	 	display_blanking_off(dev);	
		cfb_framebuffer_clear(dev, false);
		cfb_framebuffer_set_font(dev, 0);
		cfb_print(dev, "OFF", 8, 8); 	// posição em multiplos 8 para y e x
		cfb_framebuffer_finalize(dev);
		k_msleep(2000);
		display_blanking_on(dev);	// desliga visualmente o display ou "blanking" ON!
		smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Idle]);
		// desliga celulas, bombas e LEDs etc
 		// ...
	}

/* chamado para display mensagens de algum alarme do equipammento  */
static void st_alarms_run(void *o){
	//smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_BT_Temp]);
	/* 	display_blanking_off(dev);
		cfb_framebuffer_clear(dev, false);
		cfb_framebuffer_set_font(dev, 0);
		cfb_print(dev, "OFF ", 16, 8); 	// posição em multiplos 8 para y e x
		cfb_framebuffer_finalize(dev);
	*/ // Pensar o que fazer depois de mostrar alarmes.. desliga as coisas? Desliga Equipamento?	
	}


/* Display msg quando botão Temperatura é acionado. Faz uso da função de apresentação de texto corrido */
static void st_bt_temp_run(void *o){
		if (low_temp_trabalho) display_corrido("Tecla T-35", TEXT_PERIOD_S); // somente para tam fonte = 0. Função de apresentação texto corrido 
		else display_corrido("Tecla T0", TEXT_PERIOD_S);
	smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Temp]);	// depois da msg do botão flow volta a apresentar a temperatura no dipslay
	}


/* Display msg quando botão Flow é acionado. Faz uso da função de apresentação de texto corrido */
static void st_bt_flow_run(void *o){
		if (high_flow_trabalho) display_corrido("Tecla HiFLow", TEXT_PERIOD_S); // somente para tam fonte = 0. Função de apresentação texto corrido 
		else display_corrido("Tecla LowFlow", TEXT_PERIOD_S);
	smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Temp]);	// depois da msg do botão flow volta a apresentar a temperatura no dipslay
	}

/* chamado para display mensagens de temperatura dos sensores */
static void st_temp_run(void *o){
	char aux_buffer[5] = "-----";									// string é inicializada
		display_blanking_off(dev);									// turn display buffer off, in order to change text
		cfb_framebuffer_clear(dev, false);							// clear display text buffer
		cfb_framebuffer_set_font(dev, 0);							// set font size of display text
		sprintf(aux_buffer,"%2.1f", TempSensor_frio2);				// print to array the text with temperatura valor
		if (TempSensor_frio2 <0) cfb_print(dev,aux_buffer, 16, 0);	// write to the display buffer in this position if negative temperatura 
		else cfb_print(dev,aux_buffer, 16, 8);						// write to the display buffer in this position if positive temperatura
		cfb_framebuffer_finalize(dev);								// play the buffer in display
	smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Idle]);			// set next SMT state	
	}
	//TempSensor_frio1=0.0, TempSensor_frio2=0.0, TempSensor_quente=0.0;

	/* Esse estado não faz nada, é só para a máquina de estados por ficar fazendo laguma 
	coisa que não seja os outros  estamos */
static void st_idle_run(void *o){
	;	
	}

/* Populate state table */
static const struct smf_state demo_states[] = {
        [ST_BT_Inicio] = SMF_CREATE_STATE(NULL, st_bt_inicio_run, NULL),   	
        [ST_BT_Fim] = SMF_CREATE_STATE(NULL, st_bt_fim_run, NULL),	/* State S1 does not have an entry nether exit action */
        [ST_Alarms] = SMF_CREATE_STATE(NULL, st_alarms_run, NULL),	/* State S1 does not have an entry nether exit action */
        [ST_BT_Temp] = SMF_CREATE_STATE(NULL, st_bt_temp_run, NULL),	/* State S1 does not have an entry nether exit action */
        [ST_BT_Flow] = SMF_CREATE_STATE(NULL, st_bt_flow_run, NULL),	/* State S1 does not have an entry nether exit action */
        [ST_Temp] = SMF_CREATE_STATE(NULL, st_temp_run, NULL),	/* State S1 does not have an entry nether exit action */
        [ST_Idle] = SMF_CREATE_STATE(NULL, st_idle_run, NULL),	/* State S1 does not have an entry nether exit action */
};




/**********************************/
// *****   MAIN    ****************/
void main(void)
{
	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(dev)) {
			printk("Device %s not ready\n", dev->name);
			return;
		}
	if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO10) != 0) {
			printk("Failed to set required pixel format\n");
			return;
		}
	if (cfb_framebuffer_init(dev)) {
			printk("Framebuffer initialization failed!\n");
			return;
		}

	printk("Initialized %s\n", dev->name);

	cfb_framebuffer_clear(dev, true);
	display_blanking_off(dev);
	cfb_framebuffer_set_font(dev, 1);

	/* Set initial state - qdo o equipamento é ligado na energia ele inicia a cpu e entra entra em modo DESLIGADO */
    smf_set_initial(SMF_CTX(&s_obj), &demo_states[ST_BT_Fim]);
	printk("vai entrar While \n");
	inicializa_GPIO_Interrupcao();	// Configura Buttons e interrupção dos buttons BT_LIGA_DESL, BT_FLOW, BT_TEMP


	while (1) 
	{
		ret = smf_run_state(SMF_CTX(&s_obj));
                /*if (ret) {// handle return code and terminate state machine 
                        printk("State Machine parou  \n"); 
						break;
				} 
				*/
		k_msleep(1);
	}
	
}



/* Função escreve em Display de forma corrida
 * Toma por base um buffer tipo char de tamanho xx, delay entre deslocamentos
 * é usado apenas para letras grandes
 */
void display_corrido (char *buffer, int atraso){

	char aux_buffer[5] = "     ";	// string é inicializada com espaço apenas
	unsigned int size = strlen (buffer); 

	display_blanking_off(dev);
	cfb_framebuffer_set_font(dev, 0);
	for (int i=0; i < (size+1); i++){
			cfb_framebuffer_clear(dev, false);
			strncpy(aux_buffer, buffer+i,4);
			cfb_print(dev,aux_buffer, 0, 0); 	// posição em multiplos 8 para y e x
			cfb_framebuffer_finalize(dev);
			k_msleep(atraso);		
		}

}    //Modo Temp 0 graus


/************************/
// THREAD Leitura de Sensore de Temperatura
/************************/
// A thread é acordada a cada  SLEEP_TEMPERATURA_MS milisegundos
// E olha que legal, o #error é uma macro de compilação, que indica erro de compilação caso o if abaixo seja satisfeito!
// e essa é a estrutura, cada comando numa linha

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
	#error "No suitable devicetree overlay specified"
	#endif


	#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

	// Data of ADC io-channels specified in devicetree. 
	static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
	};



void temp_sensors(void)		//funçção executada como thread
{
	int err;
	int16_t buf;
	double aux;
	double aux_A=25.0/310.0;			// aux_A e aux_B calculam as constantes para gera uma equção de reta 
	double aux_B=25.0-(931.0*aux_A);	// Y = aux_A.X + aux_B oude y é temperatura e x valor do adc em bits 
										// equação de reta para o sensor TMP36 para conversão de BITs em Temperatura
	k_msleep(2000);
	printk(" -----    Thread sensores ----    \n"); 

	struct adc_sequence sequence = {
		.buffer = &buf,
		// buffer size in bytes, not number of samples 
		.buffer_size = sizeof(buf),
	};

// Configure channels individually prior to sampling. 
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			printk("ADC controller device not ready\n");
			return;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			printk("Could not setup channel #%d (%d)\n", i, err);
			return;
		}
	}

			// O loop é temporizado por uma SLEEP de "SLEEP_TEMPERATURA_MS" milisegundos para leitura dos sensores via ADC imput	 
	while (1) {
		if (sistem_ON)	{// se equipamento estiver ligado 
			for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {	// for faz leitura de todos os ADC identificados para o projeto

				/*printk("- %s, channel %d: \n",
					adc_channels[i].dev->name,
					adc_channels[i].channel_id);
				*/

				(void)adc_sequence_init_dt(&adc_channels[i], &sequence);	//incia o ADC	

				err = adc_read(adc_channels[i].dev, &sequence);				// faz leitura de ADC
				if (err < 0) {
					printk("Could not read (%d)\n", err);					// print em caso de erro
					smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Alarms]);	// seleciona SMT state para apresentação de falha	
					continue;
				} else {													// transfere os valores lidos dos ADCs para varíavei globais
						aux = (aux_A*buf) + aux_B;			// equação de reta do sensor TMP36 para conversão de BITs em Temperatura 
						printf("Temp: %2.1foC  ", aux);		// printk para controle
						if (adc_channels[i].channel_id==2) TempSensor_quente=aux;		// transfere valor para variavel sensor do bloco quente
						if (adc_channels[i].channel_id==6) TempSensor_frio1=aux;		// transfere valor para variavel sensor do bloco frio 1
						if (adc_channels[i].channel_id==7) TempSensor_frio2=aux;		// transfere valor para variavel sensor do bloco frio 2
						smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_Temp]);		
				}
			}
		}	
		printf("\n \n");
		k_msleep(SLEEP_TEMPERATURA_MS);
	}
}
K_THREAD_DEFINE(temp_sensors_id, STACKSIZE, temp_sensors, NULL, NULL, NULL, PRIORITY, 0, SLEEP_TEMPERATURA_MS); // define a thread temp_sensors, com 2 segundos de retardo 	





/*******************************************/
// BOTÔES BT_LIG_DES, BT_FLOW, BT_TEMP e suas INTERRUPÇões 
/*******************************************/

// Função que será executada na interrupção do botão BT_LIG_DESL 
// Se acioanado uma vez LIGA o equipamento, se acionado por XX segundos desliga o equipamento. 
// A variávelglobal sistem_ON indica se o equipamento se encontra ligado ou desligado
void work_handler(struct k_work *work){
	int time_aux=0; 
	
	if (sistem_ON){		// se equipamento está ligado então vai desliGar ao acionamento do BT_LIGA_DESL 
		gpio_pin_set_dt(&led, 0);			// put desabilita saida LED
		while(gpio_pin_get_dt(&button)==1){	// testa se botão BT_LIGA_DESL fica mais de XX segundos pressionados 
				k_msleep(50);				// então equipamento será desligado, se ficar menos de XX segundos não será desligado.
				time_aux = time_aux +50;
				if (time_aux > BUTTON_OFF_TIME) break;
		}
		if(time_aux > BUTTON_OFF_TIME){
			//printk("Botao + de 3 seg\n");
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
			gpio_pin_toggle_dt(&led);
			k_msleep(300);
		
			sistem_ON = false;				// indica o ifrmware que o equipamento está desligado
			smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_BT_Fim]);	// STM em estado de Desligamento do Equipamento
		

		}
	}
	else {	// Sistema está desligado e será ligado - sistema de refrigerção e bomba
		sistem_ON = true;					// indica o ifrmware que o equipamento está ligado
		smf_set_state(SMF_CTX(&s_obj), &demo_states[ST_BT_Inicio]);		// STM em estado de Ligar o Equipamento
	}
}
K_WORK_DEFINE(my_work, work_handler);

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
	k_work_submit(&my_work);
}


void inicializa_GPIO_Interrupcao(void){

    if (!device_is_ready(led.port)) {	// verifica se saida LED está OK
        return;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);	// configura saida LED
	if (ret < 0) {
		return;
	}
	gpio_pin_set_dt(&led, 0);			// desabilita saida LED

    gpio_pin_configure_dt(&button, GPIO_INPUT);		//configura entrada BUTTON
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);		// relaciona INTERRUPÇÂO ao BUTTON 

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));	// inicializa interrupção com a função a ser executada na interrução
	gpio_add_callback(button.port, &button_cb_data);			// relaciona interrupção com a função a ser executada na interrução
}

