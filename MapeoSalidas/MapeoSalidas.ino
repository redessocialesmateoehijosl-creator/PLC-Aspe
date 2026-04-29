// ==========================================
//   MapeoSalidas.ino
//
//   Programa auxiliar para identificar y
//   etiquetar todas las salidas del PLC
//   físicamente, una por una.
//
//   USO:
//   1. Abre ESTE sketch en Arduino IDE
//      (carpeta MapeoSalidas)
//   2. Sube al PLC
//   3. Abre Monitor Serie a 115200 baud
//   4. Sigue las instrucciones en pantalla
//   5. Al terminar, copia el resumen y
//      enviaselo a Claude
// ==========================================

#include <Arduino.h>

// ==========================================
//   TABLA DE SALIDAS
// ==========================================
const int PINES[] = {
  Q0_0, Q0_1, Q0_2, Q0_3, Q0_4, Q0_5, Q0_6, Q0_7,
  Q1_0, Q1_1, Q1_2, Q1_3, Q1_4, Q1_5, Q1_6, Q1_7,
  Q2_0, Q2_1, Q2_2, Q2_3, Q2_4, Q2_5, Q2_6, Q2_7
};

const char* NOMBRES[] = {
  "Q0_0", "Q0_1", "Q0_2", "Q0_3", "Q0_4", "Q0_5", "Q0_6", "Q0_7",
  "Q1_0", "Q1_1", "Q1_2", "Q1_3", "Q1_4", "Q1_5", "Q1_6", "Q1_7",
  "Q2_0", "Q2_1", "Q2_2", "Q2_3", "Q2_4", "Q2_5", "Q2_6", "Q2_7"
};

const int NUM_SALIDAS = 24;

// ==========================================
//   ESTADO
// ==========================================
String notas[NUM_SALIDAS];
int    indice    = 0;
bool   terminado = false;


// ==========================================
//   FUNCIONES AUXILIARES
// ==========================================

// Apaga todas las salidas y enciende solo la del índice dado.
// Si idx >= NUM_SALIDAS, las apaga todas (fin del proceso).
void activarSalida(int idx) {
  for (int i = 0; i < NUM_SALIDAS; i++) {
    digitalWrite(PINES[i], LOW);
  }
  if (idx < NUM_SALIDAS) {
    digitalWrite(PINES[idx], HIGH);
  }
}

void imprimirSeparador() {
  Serial.println("------------------------------------------");
}

void imprimirResumen() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("          MAPA DE SALIDAS PLC");
  Serial.println("==========================================");
  for (int i = 0; i < NUM_SALIDAS; i++) {
    String nota = (notas[i].length() > 0) ? notas[i] : "(sin anotar)";
    // Padding para alinear las columnas
    String nombre = String(NOMBRES[i]);
    while (nombre.length() < 6) nombre += " ";
    Serial.println("  " + nombre + "->  " + nota);
  }
  Serial.println("==========================================");
  Serial.println("  Copia este bloque y enviaselo a Claude");
  Serial.println("==========================================");
}

void preguntarSalida() {
  Serial.println();
  imprimirSeparador();
  Serial.println("  ACTIVA:  " + String(NOMBRES[indice]) +
                 "   (" + String(indice + 1) + " / " + String(NUM_SALIDAS) + ")");
  imprimirSeparador();
  if (notas[indice].length() > 0) {
    Serial.println("  (nota anterior: \"" + notas[indice] + "\")");
  }
  Serial.println("  Comandos: '-' saltar | 'ATR' volver | 'FIN' terminar");
  Serial.print("  Que es esta salida? -> ");
}


// ==========================================
//   SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inicializar todas las salidas apagadas
  for (int i = 0; i < NUM_SALIDAS; i++) {
    pinMode(PINES[i], OUTPUT);
    digitalWrite(PINES[i], LOW);
    notas[i] = "";
  }

  Serial.println();
  Serial.println("==========================================");
  Serial.println("      MAPEADOR DE SALIDAS PLC v1.0");
  Serial.println("==========================================");
  Serial.println("  Ira activando cada salida una a una.");
  Serial.println("  Escribe lo que ves conectado y pulsa");
  Serial.println("  Enter para pasar a la siguiente.");
  Serial.println("==========================================");

  // Primera salida
  activarSalida(0);
  preguntarSalida();
}


// ==========================================
//   LOOP
// ==========================================
void loop() {
  if (terminado) return;

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    // Eco de lo escrito (el monitor serie no lo muestra solo)
    Serial.println(input);

    // --- COMANDO: terminar y mostrar resumen ---
    if (input.equalsIgnoreCase("FIN")) {
      activarSalida(NUM_SALIDAS); // Apaga todas
      Serial.println("  Proceso terminado por el usuario.");
      imprimirResumen();
      terminado = true;
      return;
    }

    // --- COMANDO: volver a la salida anterior ---
    if (input.equalsIgnoreCase("ATR")) {
      if (indice > 0) {
        indice--;
        activarSalida(indice);
        preguntarSalida();
      } else {
        Serial.println("  (Ya estas en la primera salida)");
        Serial.print("  Que es esta salida? -> ");
      }
      return;
    }

    // --- Guardar nota ('-' = dejar en blanco) ---
    if (input == "-") {
      notas[indice] = "";
    } else if (input.length() > 0) {
      notas[indice] = input;
    }
    // Si el usuario pulsa Enter sin escribir nada, se repite la pregunta
    else {
      Serial.print("  Que es esta salida? -> ");
      return;
    }

    // Avanzar al siguiente índice
    indice++;

    // --- FIN: todas las salidas revisadas ---
    if (indice >= NUM_SALIDAS) {
      activarSalida(NUM_SALIDAS); // Apaga todas
      Serial.println();
      Serial.println("  Todas las salidas revisadas.");
      imprimirResumen();
      terminado = true;
      return;
    }

    // Siguiente salida
    activarSalida(indice);
    preguntarSalida();
  }
}
