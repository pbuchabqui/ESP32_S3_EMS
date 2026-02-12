# Mapeamento de Dependências da ECU ESP32-P4

## Tabela de Dependências entre Módulos

| Módulo Fonte | Módulo Destino | Tipo de Dependência | Descrição |
|--------------|----------------|---------------------|-----------|
| PCNT/ETM | Sync | Hardware → Software | PCNT conta pulsos, ETM decodifica posição |
| Sync | Engine Control | Software → Software | Sincronismo fornece ângulo para controle |
| ADC DMA | Sensors | Hardware → Software | ADC lê sensores, DMA transporta dados |
| Sensors | Engine Control | Software → Software | Dados dos sensores usados no controle |
| Engine Control | MCPWM | Software → Hardware | Lógica de controle gera comandos MCPWM |
| LP Core | Engine Control | Software → Software | Lógica de partida inicializa sistema |
| RTC RAM | LP Core | Hardware → Software | Armazena estado durante transição |
| Engine Control | Safety Monitor | Software → Software | Controle monitorado por segurança |
| Safety Monitor | Todos | Software → Software | Segurança monitora todos os módulos |

## Diagrama de Fluxo de Dados

```
Sensor → ADC DMA → Filtragem → Compensação → Processamento → Controle → Atuadores
   |        |         |          |           |          |         |
   v        v         v          v           v          v         v
 60-2     Contagem   Eventos    Ângulo    Massa     Injeção   Ignição
```

## Dependências Lógicas

### Dependência A: Cálculo de Ignição
- **Nível:** 3 (Detalhado)
- **Depende de:** PCNT/ETM (Nível 2)
- **Descrição:** O cálculo do avanço de ignição depende da precisão do sincronismo angular fornecido pelo PCNT e ETM.

### Dependência B: Malha Fechada de Lambda
- **Nível:** 3 (Detalhado)
- **Depende de:** ADC/DMA (Nível 2)
- **Descrição:** A estabilidade da malha de lambda depende da leitura estável e precisa dos sensores via ADC com DMA.

### Dependência C: Handover
- **Nível:** 2 (Estrutura)
- **Depende de:** RTC RAM (Nível 3)
- **Descrição:** A transição do LP Core para o HP Core depende da integridade dos dados armazenados na RTC RAM.

## Matriz de Prioridades

| Módulo | Prioridade | Motivo |
|--------|------------|---------|
| PCNT/ETM | Alta | Essencial para sincronismo |
| ADC DMA | Alta | Essencial para controle de malha fechada |
| Sync | Alta | Fundamenta todo o sistema |
| Engine Control | Alta | Núcleo da lógica de controle |
| Sensors | Média | Suporta controle de malha fechada |
| MCPWM | Alta | Executa comandos de controle |
| LP Core | Média | Lógica de partida |
| RTC RAM | Média | Persistência de estado |
| Safety Monitor | Alta | Garantia de segurança |

## Fluxos Críticos

### Fluxo 1: Sincronismo
```
Sensor → PCNT → ETM → Sync → Engine Control → MCPWM
```

### Fluxo 2: Controle de Malha Fechada
```
Sensor → ADC DMA → Sensors → Engine Control → MCPWM
```

### Fluxo 3: Partida
```
LP Core → RTC RAM → Sync → Engine Control → MCPWM
```

## Impacto de Falhas

| Módulo | Impacto | Severidade |
|--------|---------|------------|
| PCNT/ETM | Perda de sincronismo | Crítico |
| ADC DMA | Controle instável | Alto |
| Sync | Sistema inoperante | Crítico |
| Engine Control | Desempenho degradado | Médio |
| MCPWM | Sem atuação | Crítico |
| Safety Monitor | Risco de segurança | Crítico |

---

*Documento atualizado em: 31/01/2026*
*Versão: 1.0*