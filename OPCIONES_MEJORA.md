# Opciones de mejora del sistema Walia

---

## OPCIÓN 1 — Analizador de conectividad en el panel de administrador

### Objetivo
Añadir un test de velocidad/latencia en el panel admin de la app que mida la conexión tanto del móvil como del cuadro eléctrico (PLC), y ofrezca conclusiones automáticas sobre dónde está el cuello de botella.

### Arquitectura

Hay dos "trozos de internet" independientes que pueden ir lentos:
- **Internet del cuadro** (finca): PLC → router finca → internet → servidor Node-RED
- **Internet del móvil**: app → internet → servidor Node-RED

### Qué se mide y cómo

**Lado móvil — test completo (app hace el test directamente):**
- Latencia: N peticiones HTTP a `/speedtest/ping` → RTT medio
- Descarga: GET de 500 KB desde `/speedtest/download` → Mbps
- Subida: POST de 200 KB a `/speedtest/upload` → Mbps
- 100% TypeScript en Angular, sin dependencias externas

**Lado PLC — solo latencia (Arduino Mega no puede hacer transfers de banda):**
- Nuevo topic MQTT: `/{MAC}/speedtest`
- App ordena a Node-RED que lance N pings MQTT al PLC con timestamp
- PLC responde inmediatamente con eco del mismo payload
- Node-RED mide RTT + % de paquetes perdidos
- Publica resultado a la app vía Firebase o WebSocket

### Cambios por capa

| Capa | Cambio |
|------|--------|
| Node-RED | 3 endpoints HTTP nuevos: `/speedtest/ping`, `/speedtest/download`, `/speedtest/upload` + lógica de test MQTT al PLC |
| PLC (Arduino) | Responder al topic `/{MAC}/speedtest` con eco inmediato del payload |
| nodeRed.service.ts | Métodos para los 3 endpoints del test móvil |
| admin-panel (Angular) | Nuevo panel "Test de conectividad" con progreso por fases y cuadro de conclusiones |

### Flujo del test

```
[Admin pulsa "Iniciar test"]
    ↓
1. Test del móvil (fetch directo desde la app al servidor)
   · Latencia  → N pings HTTP → RTT medio
   · Descarga  → GET 500KB   → Mbps
   · Subida    → POST 200KB  → Mbps
    ↓
2. Test del PLC (orquestado vía Node-RED + MQTT)
   · N pings MQTT al PLC con timestamps
   · PLC responde inmediatamente
   · Node-RED calcula RTT medio + % pérdida
   · Resultado llega a la app
    ↓
3. Conclusiones automáticas
```

### Tabla de conclusiones

| Condición | Diagnóstico |
|-----------|-------------|
| Móvil lento (< 1 Mbps o > 300ms) + cuadro OK | "El problema está en tu conexión móvil" |
| Cuadro lento (> 500ms RTT o > 20% pérdida) + móvil OK | "El problema está en el internet de la finca" |
| Ambos lentos | "Ambas conexiones tienen problemas" |
| Ambos OK | "Las conexiones son buenas — el problema puede ser otro" |

### Limitación principal
El Arduino Mega no puede medir ancho de banda real (descarga/subida) desde el PLC por falta de RAM y potencia. Solo se puede medir latencia MQTT (que es el dato más relevante para el control del motor).

---

## OPCIÓN 2 — Migración del PLC de Arduino Mega (M-Duino 21+) a ESP32

### Objetivo
Sustituir el M-Duino 21+ (Arduino Mega, 1 núcleo, 8KB RAM) por un PLC basado en ESP32 (2 núcleos, 520KB RAM, WiFi/Ethernet) para resolver de raíz los problemas de timing de un solo núcleo y habilitar diagnósticos de red completos desde el propio PLC.

### Motivación
Los bugs de timing que han aparecido (RUN Modbus perdido, inhibición de DHCP durante movimiento, bloqueos de socket sin cable) son síntomas del mismo problema: todo corre en serie en un único núcleo de 16MHz. Con dual-core y FreeRTOS desaparecen estructuralmente.

### Qué se gana

| Mejora | Arduino Mega | ESP32 |
|--------|-------------|-------|
| Núcleos | 1 (16MHz) | 2 (240MHz) |
| RAM | 8 KB | 520 KB |
| Motor/red separados | No (serie) | Sí (Core 0 / Core 1) |
| Test de velocidad real | No (sin RAM) | Sí (HTTP GET/POST) |
| OTA (firmware por WiFi) | No | Sí |
| ArduinoJson / lambdas | No | Sí |
| Preferences (vs EEPROM) | No | Sí |

### Arquitectura FreeRTOS propuesta

- **Core 1 — Task Motor**: encoder, VFD Modbus, lógica de movimiento. Nunca interrumpido por red.
- **Core 0 — Task Red**: WiFi/Ethernet, MQTT, reconexiones, diagnósticos, speedtest.

### Consideraciones

- El proyecto ya tiene una rama `FreeRTOS` en git — no se empieza de cero.
- Los pines del ESP32 PLC (Industrial Shields) son distintos al M-Duino 21+ → requiere remapeo y revisión de cableado físico.
- Si el cuadro tiene WiFi con interferencias (variadores, fluorescentes), preferir modelo ESP32 PLC con Ethernet (W5500 externo).
- El test de velocidad completo (descarga + subida + latencia) pasa a ser posible directamente desde el PLC vía HTTP.

### Estado
**Pendiente — a realizar en el futuro.**
