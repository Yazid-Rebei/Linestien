#include <Arduino.h>

// ==========================================
// 1. CONFIGURATION DES CAPTEURS IR (16 PINS)
// ==========================================
const int NUM_SENSORS = 16;
const int irPins[NUM_SENSORS] = {
  // Module IR 1 (Gauches / Avant)
  23, 22, 21, 19, 18, 15, 17, 16,
  // Module IR 2 (Droits / Arrière)
  4, 32, 33, 13, 14, 27, 26, 25
};

// Poids des capteurs pour la moyenne pondérée (de -7.5 à +7.5)
const float weights[16] = {
  -7.5, -6.5, -5.5, -4.5, -3.5, -2.5, -1.5, -0.5,
   0.5,  1.5,  2.5,  3.5,  4.5,  5.5,  6.5,  7.5
};

// ==========================================
// 2. CONFIGURATION DU PONT EN H (MOTORS)
// ==========================================
const int ENA = 12; // PWM Vitesse Moteur Gauche
const int IN1 = 2;  // Sens Moteur Gauche
const int IN2 = 0;

const int ENB = 5;  // PWM Vitesse Moteur Droit
const int IN3 = 4;  // Sens Moteur Droit (Ajustez les pins si besoin)
const int IN4 = 0; 

// Paramètres PWM pour ESP32
const int PWM_FREQ = 5000;
const int PWM_RES = 8;
const int PWM_CHANNEL_LEFT = 0;
const int PWM_CHANNEL_RIGHT = 1;

// ==========================================
// 3. VARIABLES PID & STRATÉGIE
// ==========================================
float Kp = 25.0, Ki = 0.0, Kd = 15.0;
float error = 0, lastError = 0, integral = 0;
int baseSpeed = 180;

bool invertedMode = false;
unsigned long lastLineTime = 0;

// ==========================================
// FONCTIONS DE COMMANDE DES MOTEURS
// ==========================================
void setMotors(int speedLeft, int speedRight) {
  speedLeft = constrain(speedLeft, -255, 255);
  speedRight = constrain(speedRight, -255, 255);

  // Moteur Gauche
  if (speedLeft >= 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    ledcWrite(PWM_CHANNEL_LEFT, speedLeft);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    ledcWrite(PWM_CHANNEL_LEFT, -speedLeft);
  }

  // Moteur Droit
  if (speedRight >= 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    ledcWrite(PWM_CHANNEL_RIGHT, speedRight);
  } else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
    ledcWrite(PWM_CHANNEL_RIGHT, -speedRight);
  }
}

void applyPID(float currentError) {
  float pTerm = Kp * currentError;
  float dTerm = Kd * (currentError - lastError);
  integral += currentError;
  float iTerm = Ki * integral;

  float correction = pTerm + iTerm + dTerm;
  lastError = currentError;

  int leftSpeed = baseSpeed + correction;
  int rightSpeed = baseSpeed - correction;

  setMotors(leftSpeed, rightSpeed);
}

// ==========================================
// SETUP ET LOOP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Initialisation des capteurs IR
  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(irPins[i], INPUT);
  }

  // Initialisation des moteurs
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  ledcSetup(PWM_CHANNEL_LEFT, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENA, PWM_CHANNEL_LEFT);

  ledcSetup(PWM_CHANNEL_RIGHT, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENB, PWM_CHANNEL_RIGHT);

  setMotors(0, 0);
}

void loop() {
  int rawValues[NUM_SENSORS];
  int activeCount = 0;
  float positionSum = 0;

  // 1. Lecture avec prise en compte du mode inversé (Fond Noir / Ligne Blanche)
  for (int i = 0; i < NUM_SENSORS; i++) {
    int val = digitalRead(irPins[i]);
    
    if (invertedMode) {
      val = !val; 
    }
    
    rawValues[i] = val;
    if (val == LOW) { // LOW = Ligne détectée
      activeCount++;
      positionSum += weights[i];
    }
  }

  // 2. Détection de zone inversée (Fond noir)
  if (activeCount >= 13 && !invertedMode) {
    invertedMode = true;
  } else if (activeCount <= 2 && invertedMode) {
    invertedMode = false;
  }

  // 3. Traitement des cas particuliers (Ligne coupée vs Suivi PID)
  if (activeCount == 0) {
    // Cas de ligne coupée : continuer sur la dernière erreur pendant un court instant
    if (millis() - lastLineTime < 300) {
      applyPID(lastError); 
    } else {
      // Si la ligne n'est toujours pas trouvée, ralentir en ligne droite
      setMotors(80, 80);
    }
  } else {
    lastLineTime = millis();
    
    // Calcul de l'erreur pondérée
    error = positionSum / activeCount;

    // Appliquer le calcul PID et déplacer les moteurs
    applyPID(error);
  }
}