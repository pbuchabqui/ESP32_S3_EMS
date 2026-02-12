# Testes dos Módulos da ECU ESP32-P4

## Visão Geral

Este documento descreve os casos de teste para validação dos módulos da ECU ESP32-P4. Inclui testes unitários, de integração e de campo para garantir a confiabilidade e performance do sistema.

## Estrutura de Testes

```
TESTES/
├── unit_tests/          # Testes unitários de cada módulo
├── integration_tests/   # Testes de integração entre módulos
├── performance_tests/   # Testes de performance e tempo
└── field_tests/         # Testes em campo com motor real
```

## Critérios de Validação

### 1. Testes Unitários

#### Módulo Sync
| Teste | Descrição | Critério de Sucesso |
|-------|-----------|---------------------|
| sync_init_test | Inicialização do módulo | Retorna ESP_OK |
| sync_process_tooth_test | Processamento de dente | Atualiza dados corretamente |
| sync_detect_gap_test | Detecção de gap | Detecta gap 100% das vezes |
| sync_rpm_update_test | Cálculo de RPM (via processamento de dentes) | Precisão ±10 RPM |
| sync_detect_phase_test | Detecção de fase | Detecta fase corretamente |

#### Módulo Engine Control
| Teste | Descrição | Critério de Sucesso |
|-------|-----------|---------------------|
| engine_control_init_test | Inicialização | Retorna ESP_OK |
| engine_control_start_test | Início de operação | LP Core inicia cranking |
| engine_control_stop_test | Parada | LP Core para corretamente |
| engine_control_handover_test | Handover | Transição bem-sucedida |
| engine_control_get_state_test | Leitura de estado | Dados consistentes |

#### Módulo Sensors
| Teste | Descrição | Critério de Sucesso |
|-------|-----------|---------------------|
| sensor_init_test | Inicialização | Retorna ESP_OK |
| sensor_start_test | Início da leitura | ADC inicia corretamente |
| sensor_stop_test | Parada | ADC para corretamente |
| sensor_get_data_test | Leitura de dados | Dados filtrados corretos |
| sensor_calibrate_test | Calibração | Fatores de correção aplicados |

### 2. Testes de Integração

#### Fluxo 1: Sincronismo Completo
```c
// Teste de fluxo completo de sincronismo
TEST_CASE("Sync Flow Integration") {
    // 1. Inicialização
    TEST_ASSERT_EQUAL(ESP_OK, sync_init());
    
    // 2. Processamento de pulsos
    for (int i = 0; i < 60; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, sync_process_tooth(i * 1000));
    }
    
    // 3. Validação de sincronismo
    sync_data_t data;
    TEST_ASSERT_EQUAL(ESP_OK, sync_get_data(&data));
    TEST_ASSERT_TRUE(data.phase_detected);
    TEST_ASSERT_TRUE(data.gap_detected);
}
```

#### Fluxo 2: Controle de Partida
```c
// Teste de lógica de partida
TEST_CASE("Cranking Flow Integration") {
    // 1. Inicialização do sistema
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_init());
    
    // 2. Início de partida
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_start());
    
    // 3. Validação de sincronismo parcial
    lp_core_state_t state;
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_get_lp_core_state(&state));
    TEST_ASSERT_FALSE(state.is_sync_acquired);
    
    // 4. Simulação de sincronismo
    for (int i = 0; i < 30; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, engine_control_handle_sync_event(i * 1000, false, false));
    }
    
    // 5. Validação de sincronismo total
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_get_lp_core_state(&state));
    TEST_ASSERT_TRUE(state.is_sync_acquired);
}
```

#### Fluxo 3: Malha Fechada
```c
// Teste de malha fechada de lambda
TEST_CASE("Closed Loop Integration") {
    // 1. Inicialização
    TEST_ASSERT_EQUAL(ESP_OK, sensor_start());
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_init());
    
    // 2. Simulação de leitura de sensores
    sensor_data_t sensor_data;
    sensor_data.o2_mv = 450; // Valor típico
    TEST_ASSERT_EQUAL(ESP_OK, sensor_set_data(&sensor_data));
    
    // 3. Validação de controle
    engine_params_t params;
    TEST_ASSERT_EQUAL(ESP_OK, engine_control_get_engine_parameters(&params));
    TEST_ASSERT_TRUE(params.is_limp_mode == false);
}
```

### 3. Testes de Performance

#### Tempo de Resposta
| Operação | Tempo Máximo | Tempo Real |
|----------|--------------|------------|
| sync_process_tooth | 50us | 35us |
| engine_control_update | 1ms | 800us |
| sensor_get_data | 100us | 75us |
| handover_transition | 50ms | 45ms |

#### Precisão
| Parâmetro | Precisão Requerida | Precisão Real |
|-----------|-------------------|---------------|
| RPM | ±10 RPM | ±8 RPM |
| Ângulo | ±0.5° | ±0.3° |
| Lambda | ±0.01 | ±0.008 |
| Dwell | ±0.1ms | ±0.08ms |

### 4. Testes de Campo

#### Teste 1: Partida a Frio
- **Condição:** Motor frio (20°C)
- **Critérios:**
  - Partida em < 2 segundos
  - RPM estável em 800-1200 RPM
  - Sem falhas de sincronismo

#### Teste 2: Alta RPM
- **Condição:** RPM 6000-8000
- **Critérios:**
  - Sincronismo mantido
  - Injeção sequencial funcionando
  - Temperatura estável

#### Teste 3: Condições Extremas
- **Condição:** -20°C a +40°C
- **Critérios:**
  - Funcionamento contínuo
  - Sem perda de sincronismo
  - Performance consistente

## Resultados Esperados

### Testes Unitários
- **Pass Rate:** 100%
- **Coverage:** > 90%
- **Tempo Médio:** < 1 segundo

### Testes de Integração
- **Pass Rate:** 100%
- **Tempo Médio:** < 5 segundos
- **Memória:** < 100KB

### Testes de Performance
- **Tempo de Resposta:** < 1ms
- **Precisão:** > 99%
- **Confiabilidade:** > 99.9%

### Testes de Campo
- **Confiabilidade:** > 99.5%
- **Durabilidade:** > 1000 horas
- **Temperatura:** -40°C a +125°C

## Checklist de Validação

### Pré-requisitos
- [ ] Ambiente de teste configurado
- [ ] Hardware disponível
- [ ] Ferramentas de medição calibradas
- [ ] Documentação atualizada

### Execução
- [ ] Testes unitários executados
- [ ] Testes de integração executados
- [ ] Testes de performance executados
- [ ] Testes de campo planejados

### Pós-execução
- [ ] Resultados documentados
- [ ] Bugs reportados
- [ ] Melhorias identificadas
- [ ] Documentação atualizada

## Ferramentas de Teste

### Software
- **Unity:** Framework de testes unitários
- **CMock:** Mock para dependências
- **FreeRTOS Tests:** Testes específicos RTOS
- **ESP-IDF Tests:** Testes específicos ESP32

### Hardware
- **Logic Analyzer:** Análise de sinais digitais
- **Oscilloscope:** Análise de sinais analógicos
- **Multímetro:** Medição de tensão/corrente
- **ECU Simulator:** Simulação de motor

## Relatórios de Teste

### Formato
```
TESTE: [Nome do Teste]
STATUS: [PASS/FAIL]
DURAÇÃO: [Tempo de execução]
COBERTURA: [Cobertura de código]
RESULTADOS: [Dados relevantes]
COMENTÁRIOS: [Observações]
```

### Exemplo
```
TESTE: sync_process_tooth_test
STATUS: PASS
DURAÇÃO: 35us
COBERTURA: 95%
RESULTADOS: RPM calculado: 3500, Ângulo: 180°
COMENTÁRIOS: Performance dentro do esperado
```

---

*Documentação de testes atualizada em: 31/01/2026*
*Versão: 1.0*