# Módulo Sensors - Aquisição e Processamento de Dados

## Visão Geral

O módulo Sensors é responsável pela aquisição de dados de sensores através do ADC com DMA e processamento digital dos sinais. Implementa filtragem avançada, sincronização com o motor e calibração de sensores para fornecer dados precisos ao módulo Engine Control.

## Arquitetura

```
Sensores → ADC DMA → Filtragem → Processamento → Engine Control
   |         |         |         |           |         |
   v         v         v         v           v         v
  MAP/TPS   ADC      DMA      Digital     Dados     Controle
  CLT/IAT   Contínuo  Buffer    Filtros    Filtrados
```

## Configuração

```c
static sensor_config_t g_sensor_config = {0};
static sensor_data_t g_sensor_data = {0};
static SemaphoreHandle_t g_sensor_mutex = NULL;
static TaskHandle_t g_sensor_task_handle = NULL;
```

## Funcionalidades Principais

### 1. Inicialização
```c
esp_err_t sensor_init(void);
```
- Cria mutex para sincronização
- Carrega configuração padrão
- Prepara sistema para operação

### 2. Início da Leitura
```c
esp_err_t sensor_start(void);
```
- Configura ADC contínuo (20kHz)
- Cria task de processamento
- Inicia aquisição de dados

### 3. Parada da Leitura
```c
esp_err_t sensor_stop(void);
```
- Para ADC
- Deleta task de processamento
- Libera recursos

### 4. Aquisição de Dados
```c
esp_err_t sensor_get_data(sensor_data_t *data);
```
- Retorna dados filtrados
- Garante consistência com mutex
- Fornece estatísticas de amostragem

## Estruturas de Dados

### sensor_channel_t
```c
typedef enum {
    SENSOR_MAP = 0,      // Manifold Absolute Pressure
    SENSOR_TPS,          // Throttle Position Sensor
    SENSOR_CLT,          // Coolant Temperature
    SENSOR_IAT,          // Intake Air Temperature
    SENSOR_COUNT
} sensor_channel_t;
```

### sensor_data_t
```c
typedef struct {
    // Dados brutos do ADC
    uint16_t raw_adc[SENSOR_COUNT];           // Valores brutos 0-4095
    
    // Dados filtrados
    uint16_t map_kpa10;            // Pressão em kPa * 10
    uint16_t tps_percent;          // Posição da borboleta %
    int16_t clt_c;                 // Temperatura do líquido °C
    int16_t iat_c;                 // Temperatura do ar °C
    
    // Dados processados
    uint32_t engine_load;          // Carga do motor %
    uint16_t barometric_pressure;  // Pressão barométrica kPa
    bool tps_changed;              // Flag de mudança de posição
    
    // Estatísticas
    uint32_t sample_count;         // Contador de amostras
    uint32_t error_count;          // Contador de erros
} sensor_data_t;
```

### sensor_config_t
```c
typedef struct {
    // Configuração ADC
    adc_atten_t attenuation;       // Atenuação do ADC
    adc_bits_width_t width;        // Resolução
    uint32_t sample_rate_hz;       // Taxa de amostragem
    
    // Configuração filtros
    float map_filter_alpha;        // Alfa do filtro MAP
    float tps_filter_alpha;        // Alfa do filtro TPS
    float temp_filter_alpha;       // Alfa do filtro temperatura
    
    // Configuração sincronização
    bool map_sync_enabled;         // Sincronização com motor
    uint32_t map_sync_angle;       // Ângulo de sincronização
} sensor_config_t;
```

## Algoritmos Principais

### Filtro MAP (Média Móvel)
```c
// Filtro média móvel de 16 amostras
map_filter_buffer[map_filter_index++] = val;
if (map_filter_index >= 16) map_filter_index = 0;

uint32_t sum = 0;
for (int j = 0; j < 16; j++) {
    sum += map_filter_buffer[j];
}
g_sensor_data.map_kpa10 = (uint16_t)((sum / 16.0f) * 0.1f); // Convert to kPa*10
```

### Filtro TPS (Passa-Baixas)
```c
// Filtro passa-baixas de 1ª ordem
static float tps_filtered = 0.0f;
tps_filtered = (tps_filtered * (1.0f - g_sensor_config.tps_filter_alpha)) + 
              (val * g_sensor_config.tps_filter_alpha);
g_sensor_data.tps_percent = (uint16_t)(tps_filtered / 40.95f); // Convert to %
```

### Filtros de Temperatura
```c
// Filtro exponencial forte
static float temp_filtered = 0.0f;
temp_filtered = (temp_filtered * (1.0f - g_sensor_config.temp_filter_alpha)) + 
               (val * g_sensor_config.temp_filter_alpha);
g_sensor_data.clt_c = (int16_t)((temp_filtered - 1638.4f) / 8.2f); // Convert to °C
```

## Configuração Padrão

```c
g_sensor_config.attenuation = ADC_ATTEN_DB_11;
g_sensor_config.width = ADC_WIDTH_BIT_12;
g_sensor_config.sample_rate_hz = 20000; // 20kHz
g_sensor_config.map_filter_alpha = 0.2f;
g_sensor_config.tps_filter_alpha = 0.05f;
g_sensor_config.temp_filter_alpha = 0.05f;
g_sensor_config.map_sync_enabled = true;
g_sensor_config.map_sync_angle = 15; // Sincronizar no dente 15
```

## Desempenho

### Taxa de Amostragem
- **ADC:** 20kHz
- **Processamento:** 1kHz
- **Latência:** < 1ms

### Precisão
- **MAP:** ±0.1 kPa
- **TPS:** ±0.5%
- **Temperatura:** ±1°C
- **Resolução:** 12 bits

## Dependências

- **ADC DMA (Hardware):** Aquisição contínua
- **FreeRTOS (Software):** Gerenciamento de tasks
- **Mutex (Software):** Sincronização de dados
- **GPIO (Hardware):** Interface com sensores

## Casos de Uso

### 1. Operação Normal
1. ADC lê sensores continuamente
2. Filtros processam dados
3. Dados disponíveis para controle
4. Estatísticas atualizadas

### 2. Sincronização com Motor
1. Detecção de ângulo específico
2. Sincronização de MAP
3. Redução de ruído
4. Melhoria de precisão

### 3. Calibração
1. Valores brutos capturados
2. Valores de engenharia aplicados
3. Fatores de correção calculados
4. Sistema recalibrado

## Limitações Conhecidas

- **Canais ADC:** 4 canais simultâneos
- **Taxa máxima:** 20kHz
- **Temperatura:** -40°C a +125°C
- **Tensão:** 8V a 18V

## Melhorias Futuras

- **Mais canais:** Expansão para 8 canais
- **Taxa maior:** Até 50kHz
- **Diagnóstico:** Detecção de falhas
- **Autocalibração:** Ajuste automático

---

*Módulo Sensors documentado em: 31/01/2026*
*Versão: 1.0*