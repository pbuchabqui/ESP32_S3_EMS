# S3 Virtual Input Simulation

Este projeto simula entradas virtuais de ECU para validar o pipeline `planner -> ring -> executor`.

## O que simula
- CKP 60-2 com gap (timer de dentes)
- Sensores virtuais por cenário (idle, aceleração, alta carga)
- Carga extra de CPU (task de load)
- Métricas de deadline para 700 us e 600 us

## Métricas reportadas
- `Planner(us)`: p95, p99, max, misses >700us e >600us
- `Executor(us)`: p95, p99, max, misses >700us e >600us
- `Queue age(us)`: max, misses >700us e >600us
- `Verdict <=700us` e `Verdict <=600us`

## Build
```powershell
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5.1'
. $env:IDF_PATH\export.ps1
cd tools\s3_jitter_sim
idf.py build
```

## Flash/Monitor
```powershell
idf.py -p COMx flash monitor
```

## Critério de aprovação
- Para janela de 700 us: `miss700 == 0` para planner, executor e queue age.
- Para janela de 600 us: `miss600 == 0` para planner, executor e queue age.