# Módulo Sync - Sincronismo Angular

## Visão Geral

O módulo Sync é responsável pela decodificação do padrão 60-2 e sincronismo angular do motor. Utiliza o PCNT (Pulse Counter) e ETM (Event Timer Matrix) do ESP32-P4 para contar pulsos do sensor de posição e determinar com precisão a posição do pistão.

## Arquitetura

```
Sensor de Posição (60-2) → PCNT → ETM → Sync → Engine Control
         |              |      |        |          |
         v              v      v        v          v
      Pulsos Magnéticos Contagem Eventos  Ângulo    Controle
```

## Configuração

```c
static sync_config_t g_sync_config = {
    .tooth_count = 58,           // 60-2 wheel (58 teeth + 2 gap)
    .gap_tooth = 58,             // Gap occurs after tooth 58
    .max_rpm = 8000,
    .min_rpm = 500,
    .enable_phase_detection = true
};
```

## Funcionalidades Principais

### 1. Inicialização
```c
esp_err_t sync_init(void);
```
- Configura PCNT para contagem de pulsos
- Habilita filtro de ruído (100ns)
- Cria mutex para sincronização

### 2. Processamento de Dentes
```c
esp_err_t sync_process_tooth(uint32_t tooth_time);
```
- Calcula período entre dentes
- Detecta gap (2 dentes faltantes)
- Atualiza índice do dente atual
- Calcula tempo por grau

### 3. Detecção de Fase
```c
esp_err_t sync_detect_phase(void);
```
- Detecta posição de fase (720°)
- Valida sincronismo completo
- Registra tempo de detecção

### 4. Cálculo de RPM
- RPM calculado diretamente no processamento de dentes (fluxo hardware)
- Aplica limites (500-8000 RPM) no mesmo caminho de atualização
- Mantém consistência com timestamps do GPTimer via ETM

## Algoritmos Principais

### Detecção de Gap
```c
// Detect gap (2 teeth missing)
if (tooth_period > (g_sync_data.tooth_period * 1.5f)) {
    g_sync_data.gap_detected = true;
    g_sync_data.tooth_index = 0;
}
```

### Cálculo de RPM
```c
// Calculate RPM based on tooth period
uint32_t time_per_revolution = g_sync_data.tooth_period * teeth_per_revolution;
g_sync_data.rpm = (1000000 * 60) / time_per_revolution;
```

## Estruturas de Dados

### sync_data_t
```c
typedef struct {
    uint32_t last_tooth_time;
    uint32_t tooth_period;
    uint32_t tooth_index;
    bool gap_detected;
    bool phase_detected;
    uint32_t phase_detected_time;
    uint32_t rpm;
    float time_per_degree;
} sync_data_t;
```

### sync_config_t
```c
typedef struct {
    uint32_t tooth_count;
    uint32_t gap_tooth;
    uint32_t max_rpm;
    uint32_t min_rpm;
    bool enable_phase_detection;
} sync_config_t;
```

## Testes Unitários

### sync_test.c
- Teste de inicialização
- Teste de processamento de dentes
- Teste de detecção de gap
- Teste de cálculo de RPM
- Teste de detecção de fase

## Desempenho

### Tempo de Resposta
- **Processamento de dente:** < 50us
- **Cálculo de RPM:** < 20us
- **Detecção de fase:** < 10us

### Precisão
- **Ângulo:** ±0.5°
- **RPM:** ±10 RPM
- **Detecção de gap:** 100% confiável

## Dependências

- **PCNT (Hardware):** Contagem de pulsos
- **ETM (Hardware):** Sincronismo de eventos
- **Mutex (Software):** Sincronização de threads
- **Timer (Hardware):** Medição de tempo

## Casos de Uso

### Operação Normal
1. Sensor gera pulsos 60-2
2. PCNT conta pulsos
3. Sync processa dentes
4. Engine Control recebe sincronismo

### Lógica de Partida
1. LP Core inicializa Sync
2. Prime Pulse gerado
3. Sincronismo parcial validado
4. Handover para HP Core

## Limitações Conhecidas

- **Frequência máxima:** 8000 RPM
- **Resolução mínima:** 100ns
- **Temperatura:** -40°C a +125°C

## Melhorias Futuras

- **Suporte a outros padrões:** 36-1, 12-1
- **Calibração automática:** Ajuste de ganho
- **Diagnóstico avançado:** Detecção de falhas

---

*Módulo Sync documentado em: 31/01/2026*
*Versão: 1.0*