# ECU ESP32-P4 - Documento Mestre

## 1. Introdução

A Unidade de Controle de Motor (ECU) baseada no ESP32-P4 representa uma solução inovadora para controle de motores de combustão interna. Utilizando o poderoso SoC ESP32-P4, esta ECU implementa funcionalidades que tradicionalmente exigiriam FPGAs ou ASICs dedicados, graças à matriz de eventos (ETM) e ao coprocessador RISC-V integrado.

### Objetivos Principais
- Controle preciso de sincronismo angular
- Injeção sequencial e ignição just-in-time
- Estratégias de partida robustas
- Controle de malha fechada de lambda
- Aquisição de dados de alta velocidade

## 2. Arquitetura Geral

```
+------------------------+     +------------------------+
|        Captura         |<--->|        Decisão         |
| (PCNT, ETM, MAX9924)   |     | (Interpolação, Tabelas)|
+------------------------+     +------------------------+
              |                           |
              v                           v
+------------------------+     +------------------------+
|        Execução        |     |        Persistência    |
| (MCPWM, Just-in-Time)  |     | (RTC RAM, LP Core)     |
+------------------------+     +------------------------+
```

### Componentes Principais
- **PCNT (Pulse Counter):** Contagem de pulsos do sensor de posição
- **ETM (Event Timer Matrix):** Matriz de eventos para sincronismo
- **MCPWM:** Controle de atuadores (injetores, bobinas)
- **ADC com DMA:** Aquisição de dados de sensores
- **LP Core:** Coprocessador para lógica de partida

## 3. Módulos Principais

### 3.1 Módulo Sync
Responsável pela decodificação do padrão 60-2 e sincronismo angular.

**Funcionalidades:**
- Detecção de posição do pistão
- Sincronismo de fase (720°)
- Tratamento de transients
- Validação de sincronismo

### 3.2 Módulo Engine Control
Núcleo da lógica de controle do motor.

**Funcionalidades:**
- Interpolação bilinear de tabelas 3D
- Cálculo de massa de combustível
- Avanço de ignição
- Estratégias de partida (Cranking, Prime Pulse)

### 3.3 Módulo Sensors
Aquisição e processamento de dados de sensores.

**Funcionalidades:**
- ADC com DMA a 1KHz
- Filtragem digital
- Compensação de bateria
- Calibração de sensores

## 4. Fluxo de Controle

### 4.1 Ciclo Normal de Operação
1. **Captura:** Sensor de posição gera pulsos
2. **Sincronismo:** PCNT/ETM decodifica posição angular
3. **Decisão:** Interpolação de tabelas 3D
4. **Execução:** MCPWM gera pulsos para atuadores
5. **Feedback:** ADC lê sensores para malha fechada

### 4.2 Lógica de Partida
1. **Inicialização LP Core:** Pré-carga de RTC RAM
2. **Prime Pulse:** Injeção única para partida
3. **Sincronismo Parcial:** Validação 360°
4. **Handover:** Transição para HP Core
5. **Sincronismo Total:** Operação normal 720°

## 5. Estratégias de Controle

### 5.1 Injeção Sequencial
- **Just-in-Time:** Pulsos gerados no momento exato
- **Dwell variável:** 2.0ms a 8.5ms conforme RPM
- **Battery Compensation:** Ajuste automático de tensão

### 5.2 Controle de Ignição
- **Avanço variável:** Baseado em RPM e carga
- **Interpolação bilinear:** Matriz 16x16 com pesos ponderados
- **Anti-detonation:** Limitação de avanço máximo

### 5.3 Malha Fechada de Lambda
- **PID com anti-windup:** Limitação de 25% de correção
- **Resposta rápida:** Ajuste em tempo real
- **Estabilidade:** Prevenção de oscilações

## 6. Requisitos de Hardware

### 6.1 Módulos de Entrada
- Sensor de posição (60-2)
- Sensor de temperatura (CLT)
- Sensor de pressão (MAP)
- Sensor de oxigênio (O2)

### 6.2 Módulos de Saída
- Injetores de combustível
- Bobinas de ignição
- Válvulas de controle
- Indicadores de diagnóstico

### 6.3 Requisitos de Processamento
- **PCNT:** Contagem precisa de pulsos
- **ETM:** Sincronismo sub-microssegundo
- **MCPWM:** Geração de pulsos de alta resolução
- **ADC DMA:** Aquisição contínua de dados

## 7. Desempenho Esperado

### 7.1 Tempo de Resposta
- **Sincronismo:** < 100us
- **Injeção:** < 50us
- **Ignição:** < 20us
- **Malha fechada:** < 1ms

### 7.2 Precisão
- **Ângulo:** ±0.5°
- **Dwell:** ±0.1ms
- **Lambda:** ±0.01
- **RPM:** ±10 RPM

### 7.3 Confiabilidade
- **Taxa de falha:** < 0.01%
- **Temperatura:** -40°C a +125°C
- **Tensão:** 8V a 18V
- **Vibração:** Até 20g

## 8. Validação e Testes

### 8.1 Testes Unitários
- Cada módulo testado individualmente
- Casos de borda validados
- Performance medida

### 8.2 Testes de Integração
- Sistema completo validado
- Interações entre módulos testadas
- Performance global medida

### 8.3 Testes de Campo
- Validação em motor real
- Condições extremas testadas
- Confiabilidade a longo prazo

---

*Documento atualizado em: 31/01/2026*
*Versão: 1.0*