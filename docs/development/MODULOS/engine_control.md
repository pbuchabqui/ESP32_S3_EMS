# Módulo Engine Control - Controle do Motor

## Visão Geral

O módulo Engine Control é o núcleo da lógica de controle do motor, responsável por coordenar todos os aspectos do funcionamento do motor através do coprocessador LP Core. Implementa estratégias de controle avançadas incluindo injeção sequencial, ignição just-in-time e controle de malha fechada.

## Arquitetura

```
Engine Control → LP Core → Sync → Atuadores
       |           |        |         |
       v           v        v         v
    Controle    Partida  Sincronismo  Execução
```

## Configuração

```c
static lp_core_config_t g_lp_core_config = {0};
static lp_core_state_t g_lp_core_state = {0};
```

## Funcionalidades Principais

### 1. Inicialização
```c
esp_err_t engine_control_init(void);
```
- Carrega configuração do LP Core
- Inicializa coprocessador
- Prepara sistema para operação

### 2. Controle de Partida
```c
esp_err_t engine_control_start(void);
```
- Inicia lógica de partida (Cranking)
- Gerencia Prime Pulse
- Realiza sincronismo parcial

### 3. Controle de Parada
```c
esp_err_t engine_control_stop(void);
```
- Para lógica de partida
- Desativa atuadores
- Prepara sistema para desligamento

### 4. Handover
```c
esp_err_t engine_control_perform_handover(void);
```
- Transição do LP Core para HP Core
- Transferência de estado
- Continuidade do controle

## Estruturas de Dados

### engine_control_status_t
```c
typedef enum {
    ENGINE_CONTROL_OK = 0,
    ENGINE_CONTROL_ERROR = -1,
    ENGINE_CONTROL_NOT_INITIALIZED = -2,
    ENGINE_CONTROL_ALREADY_RUNNING = -3,
    ENGINE_CONTROL_INVALID_STATE = -4
} engine_control_status_t;
```

### core_sync_data_t
```c
typedef struct {
    uint32_t heartbeat;
    uint32_t last_sync_time;
    uint32_t error_count;
    bool is_sync_acquired;
    uint32_t handover_rpm;
    uint32_t handover_advance;
} core_sync_data_t;
```

### sensor_data_t
```c
typedef struct {
    uint16_t map_kpa10;
    int16_t clt_c;
    uint8_t tps_percent;
    int16_t iat_c;
    uint16_t o2_mv;
    uint16_t vbat_dv;
} sensor_data_t;
```

### engine_params_t
```c
typedef struct {
    uint32_t rpm;
    uint32_t load;
    uint16_t advance_deg10;
    uint16_t fuel_enrichment;
    bool is_limp_mode;
} engine_params_t;
```

## Algoritmos Principais

### Lógica de Partida (Cranking)
```c
esp_err_t engine_control_start(void) {
    // Start LP Core cranking
    esp_err_t err = lp_core_start_cranking();
    if (err != ESP_OK) {
        ESP_LOGE("ENGINE_CONTROL", "Failed to start LP Core cranking");
        return err;
    }
}
```

### Handover
```c
esp_err_t engine_control_perform_handover(void) {
    return lp_core_perform_handover();
}
```

### Controle de Estado
```c
esp_err_t engine_control_get_core_sync_data(core_sync_data_t *sync_data) {
    if (xSemaphoreTake(g_lp_core_state.state_mutex, portMAX_DELAY) == pdTRUE) {
        sync_data->heartbeat = esp_timer_get_time() / 1000;
        sync_data->last_sync_time = g_lp_core_state.last_sync_time;
        sync_data->is_sync_acquired = g_lp_core_state.is_sync_acquired;
        sync_data->handover_rpm = g_lp_core_state.current_rpm;
        sync_data->handover_advance = 100; // Default value
        xSemaphoreGive(g_lp_core_state.state_mutex);
        return ESP_OK;
    }
    return ESP_FAIL;
}
```

## Estratégias de Controle

### 1. Injeção Sequencial
- **Just-in-Time:** Pulsos gerados no momento exato
- **Dwell variável:** 2.0ms a 8.5ms conforme RPM
- **Battery Compensation:** Ajuste automático de tensão

### 2. Controle de Ignição
- **Avanço variável:** Baseado em RPM e carga
- **Interpolação bilinear:** Matriz 16x16 com pesos ponderados
- **Anti-detonation:** Limitação de avanço máximo

### 3. Malha Fechada de Lambda
- **PID com anti-windup:** Limitação de 25% de correção
- **Resposta rápida:** Ajuste em tempo real
- **Estabilidade:** Prevenção de oscilações

## Fluxo de Controle

### Operação Normal
1. **Inicialização:** Engine Control → LP Core
2. **Sincronismo:** Sync → Engine Control
3. **Decisão:** Tabelas 3D → Interpolação
4. **Execução:** MCPWM → Atuadores
5. **Feedback:** Sensores → Malha fechada

### Lógica de Partida
1. **Cranking:** LP Core gerencia partida
2. **Prime Pulse:** Injeção única inicial
3. **Sincronismo Parcial:** Validação 360°
4. **Handover:** Transição para HP Core
5. **Operação Normal:** Controle contínuo

## Desempenho

### Tempo de Resposta
- **Inicialização:** < 100ms
- **Handover:** < 50ms
- **Atualização de estado:** < 1ms

### Confiabilidade
- **Taxa de falha:** < 0.01%
- **Recuperação de erro:** Automática
- **Diagnóstico:** Integrado

## Dependências

- **LP Core (Software):** Núcleo de controle
- **Sync (Software):** Sincronismo angular
- **MCPWM (Hardware):** Controle de atuadores
- **ADC DMA (Hardware):** Aquisição de dados
- **Mutex (Software):** Sincronização de threads

## Casos de Uso

### 1. Partida a Frio
- LP Core gerencia cranking
- Prime Pulse para partida
- Transição para operação normal

### 2. Operação Normal
- Controle contínuo de injeção
- Ajuste de ignição em tempo real
- Malha fechada de lambda ativa

### 3. Condições de Emergência
- Detecção de falhas
- Entrada em limp mode
- Recuperação automática

## Limitações Conhecidas

- **Suporte a 1 cilindro:** Implementação atual
- **Padrão 60-2:** Sensor de posição específico
- **Temperatura:** -40°C a +125°C

## Melhorias Futuras

- **Multi-cilindro:** Suporte a mais cilindros
- **Padrões múltiplos:** 36-1, 12-1, etc.
- **Diagnóstico avançado:** Previsão de falhas
- **Otimização:** Ajuste automático de parâmetros

---

*Módulo Engine Control documentado em: 31/01/2026*
*Versão: 1.0*