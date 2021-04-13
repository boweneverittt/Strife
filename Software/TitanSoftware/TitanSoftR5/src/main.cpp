#include <Arduino.h>

#include <BMI088.h>
#include <SD.h>
#include <Servo.h>

#include <Orientation.h>
#include <PID.h>

#include <DataPoint.h>
#include <FlightState.h>



Bmi088Accel accel(Wire,0x19);
Bmi088Gyro gyro(Wire,0x69);

Servo tvcZ;
Servo tvcY;

const int pyro3 = 29;
const int pyro4 = 33;
const int statusled = 13;



FlightState currentState;


const int LAUNCH_CHECK = 15;


Orientation ori;
EulerAngles gyroData;
EulerAngles gyroOut;
RawAccel accelData;

double g_bias[3];

int count = 0;
const int averageAmount = 500;

double elapsedFlightTime;

uint64_t thisLoopMicros = 0;
uint64_t lastMainLoopMicros = 0;
uint64_t logDtMicros = 0;
double dt = 0;

uint64_t nextServoMicros = 0;
const uint8_t servoHz = 16;
const uint64_t servoMicros = 1000000 / servoHz;

uint64_t lastLogUpdate = 0;
const uint8_t logHz = 100;
const uint64_t logMicros = 1000000 / logHz;

double pidYOut, pidZOut;
double angleYOut, angleZOut;
double finalAngleYOut, finalAngleZOut;
double deviationY, deviationZ;

int servoYOffset = -20;
int servoZOffset = 7;
int homeYAngle = 90;
int homeZAngle = 90;
int SGR = 6;

double kp = 0.3;
double ki = 0.25;
double kd = 0.1;
double setpoint = 0;
PID pidY = {kp, ki, kd, setpoint};
PID pidZ = {kp, ki, kd, setpoint};

File logFile;
uint8_t logFileNumber;
int failure;
// bool abort;

double flightAlt;
double fcBatt;

int pyro3Val = 0;
int pyro4Val = 0;

double temp;

int DATA_ERROR;

int test = 1;



void ledStatusBlink()
{
    digitalWrite(statusled, HIGH);
    delay(500);
    digitalWrite(statusled, LOW);
    delay(500);
    digitalWrite(statusled, HIGH);
    delay(500);
    digitalWrite(statusled, LOW);
    delay(500);
    digitalWrite(statusled, HIGH);
    delay(500);
    digitalWrite(statusled, LOW);
    delay(500);
}

void getGyroBias()
{
    while (count < averageAmount)
    {
        gyro.readSensor();
        g_bias[0] += gyro.getGyroX_rads();
        g_bias[1] += gyro.getGyroY_rads();
        g_bias[2] += gyro.getGyroZ_rads();
        count += 1;
        Serial.print(".");
        delay(10);
    }
    Serial.println();
    g_bias[0] /= (double)averageAmount;
    g_bias[1] /= (double)averageAmount;
    g_bias[2] /= (double)averageAmount;

    Serial.print(g_bias[0], 8);
    Serial.print(" ");
    Serial.print(g_bias[1], 8);
    Serial.print(" ");
    Serial.print(g_bias[2], 8);
    Serial.println(" ");
}

void startup()
{
    int status;

    Serial.begin(115200);

    delay(1000);

    status = accel.begin();
    if (status < 0) 
    {
        Serial.println("Accel Initialization Error");
        Serial.println(status);
        while (1) {}
    }

    status = gyro.begin();
    if (status < 0) 
    {
        Serial.println("Gyro Initialization Error");
        Serial.println(status);
        while (1) {}
    }
}

void setupPins()
{
    tvcY.attach(36);
    tvcZ.attach(37);
    tvcY.writeMicroseconds(1500);
    tvcZ.writeMicroseconds(1500);
    pinMode(pyro3, OUTPUT);
    pinMode(pyro4, OUTPUT);
    pinMode(statusled, OUTPUT);
}

void startTiming()
{
    thisLoopMicros = micros();
    lastMainLoopMicros = micros();
    nextServoMicros = micros() + servoMicros;
    lastLogUpdate = micros() + logMicros;
}

void setupSD()
{
  Serial.print("Initializing SD card...");

  if (!SD.begin(BUILTIN_SDCARD)) 
  {
    Serial.println("Card failed, or not present");
    return;
  }

  Serial.println("card initialized.");
}

void logTest()
{
    if (!SD.begin(BUILTIN_SDCARD))
    {
        failure |= SD_INIT_TIMEOUT;
        return;
    }
    else
    {
        char logFileName[16] = "";
        for (int i = 0; i < 256; i++)
        {
            sprintf(logFileName, "log%.3u.csv", i);
            
            if (!SD.exists(logFileName))
            {
                logFile = SD.open(logFileName, FILE_WRITE);
                logFile.println("test");
                logFile.flush();
                return;
            }
        }
    }
}

void logTestData()
{
    if (logFile)
    {
        char line[200] = "";
        sprintf(line, "test, %c", test);
        logFile.println(line);
        logFile.flush();
    }
}

void makeLogFile()
{
    if (!SD.begin(BUILTIN_SDCARD))
    {
        Serial.println("Failed to create log file");
        failure |= SD_INIT_TIMEOUT;
        return;
    }
    else
    {
        char logFileName[16] = "";
        for (int i = 0; i < 256; i++)
        {
            sprintf(logFileName, "log%.3u.csv", i);
            Serial.println("Created log file");
            if (!SD.exists(logFileName))
            {
                logFile = SD.open(logFileName, FILE_WRITE);
                logFile.println("elapsedFlightTime,flightState,battVoltage,vehicleAlt,gX,gY,gZ,aX,aY,aZ,roll,pitch,yaw,yPID,zPID,yServo,zServo,zPIDError,zPIDIntegral,zPIDDerivative,yPIDError,yPIDIntegral,yPIDDerivative,deviationY,deviationZ,temp,pyro3,pyro4,abort");
                logFile.flush();
                return;
            }
        }
    }
}

void logData()
{
   //  Serial.println("Running log data function");
    if (logFile && thisLoopMicros > lastLogUpdate + logMicros)
    {
        // Serial.println("Logging attempt!");
        char line[350] = "";
        logDtMicros = thisLoopMicros - lastLogUpdate;
        sprintf(line, "%15f,%d,%15f,%15f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%15f,%u,%u",
            ((double)thisLoopMicros / 1000000.),
            currentState,
            fcBatt,
            flightAlt,
            gyroData.roll,
            gyroData.pitch,
            gyroData.yaw,
            accelData.x,
            accelData.y,
            accelData.z,
            (gyroOut.roll * RAD_TO_DEG),
            (gyroOut.pitch * RAD_TO_DEG),
            (gyroOut.yaw * RAD_TO_DEG),
            angleYOut,
            angleZOut,
            finalAngleYOut,
            finalAngleZOut,
            pidZ.getError(),
            pidZ.getIntegral(),
            pidZ.getDerivative(),
            pidY.getError(),
            pidY.getIntegral(),
            pidY.getDerivative(),
            deviationY,
            deviationZ,
            temp,
            pyro3Val,
            pyro4Val);
        logFile.println(line);
        logFile.flush();
        // Serial.println("Data logging successful");

        lastLogUpdate = thisLoopMicros;
    }
}

void updateTiming()
{
    thisLoopMicros = micros();
    dt = (double)(thisLoopMicros - lastMainLoopMicros) / 1000000.;
    lastMainLoopMicros = thisLoopMicros;
}

void checkSensors()
{
    gyro.readSensor();
    accel.readSensor();

    gyroData.roll = gyro.getGyroX_rads();
    gyroData.pitch = gyro.getGyroY_rads();
    gyroData.yaw = gyro.getGyroZ_rads();

    accelData.x = accel.getAccelX_mss();
    accelData.y = accel.getAccelY_mss();
    accelData.z = accel.getAccelZ_mss();

    temp = accel.getTemperature_C();

    ori.update(gyroData, dt);
    gyroOut = ori.toEuler();

    gyroOut.pitch = gyroOut.pitch; // sign flip because for some reason pitch and yaw are left hand rule naturally
    gyroOut.yaw = -gyroOut.yaw;
}

void printOri()
{
    Serial.print("X "); Serial.print(gyroOut.roll * RAD_TO_DEG); Serial.print("\t");
    Serial.print("Y "); Serial.print(gyroOut.pitch * RAD_TO_DEG); Serial.print("\t");
    Serial.print("Z "); Serial.print(gyroOut.yaw * RAD_TO_DEG); Serial.print("\n");
}

void updatePID()
{
    pidYOut = pidY.update(gyroOut.pitch * RAD_TO_DEG, dt);
    pidZOut = pidZ.update(gyroOut.yaw * RAD_TO_DEG, dt);

    pidYOut = -pidYOut;
    pidZOut = -pidZOut;

    angleYOut = constrain(pidYOut, -5, 6);
    angleZOut = constrain(pidZOut, -14, 8);

    finalAngleYOut = constrain(((angleYOut * SGR) + homeYAngle) + servoYOffset, 0, 180);
    finalAngleZOut = constrain(((angleZOut * SGR) + homeZAngle) + servoZOffset, 0, 180);

    // Serial.print("finalAngleYOut: "); Serial.print(finalAngleYOut); Serial.print("\t");
    // Serial.print("finalAngleZOut: "); Serial.print(finalAngleZOut); Serial.print("\n");
    if (thisLoopMicros >= nextServoMicros)
    {
        // Serial.println("Wrote to servos");
        tvcY.write(finalAngleYOut);
        tvcZ.write(finalAngleZOut);
        nextServoMicros += servoMicros;
    }
}

/*
void checkLaunch()
{
  if(imuRawData.accelX > launchDetect) launchDetectStart = thisLoopMicros;
  if(thisLoopMicros > launchDetectStart + launchDetectTime) currentState = POWERED_FLIGHT;
  Serial.print("Accel X "); Serial.print(imuRawData.accelX); Serial.print("\t");
}
*/

void checkLaunch()
{
    if(accelData.x > LAUNCH_CHECK)
    {
        currentState = POWERED_FLIGHT;
    }
}

void checkBurnout()
{

}

void checkApogee()
{

}

void checkChuteStop()
{

}

void checkLanded()
{

}

void showSafe()
{

}

void landed()
{
    logFile.flush();
    logFile.close();

    Serial.println("Flight complete");

    // flash led and stop any other non-essential functions
}

void abortFlight()
{
    // if (currentState == ABORT)
    // {

    // }
    Serial.println("FLIGHT ABORT");
}

void setup()
{
    startup();
    setupPins();
    setupSD();
    makeLogFile();

    ledStatusBlink();

    currentState = GROUND_IDLE;
    // getGyroBias();
    startTiming();
}

void loop() 
{
    updateTiming();
    Serial.print("TIME: "); Serial.print((double)thisLoopMicros / 1000000.); Serial.print("\t");
    checkSensors();

    Serial.print("ACCELX: "); Serial.print(accelData.x); Serial.print("\t");
    Serial.print("PITCH: "); Serial.print(gyroOut.pitch * RAD_TO_DEG); Serial.print("\t");
    Serial.print("YAW: ");Serial.print(gyroOut.yaw * RAD_TO_DEG); Serial.print("\t");
    Serial.print("PIDY: "); Serial.print(angleYOut); Serial.print("\t");
    Serial.print("PIDZ: ");Serial.print(angleZOut); Serial.print("\n");

    switch(currentState)
    {
        case GROUND_IDLE:
            checkLaunch();
            break;

        case POWERED_FLIGHT:
            updatePID();
            checkBurnout();
            break;

        case UNPOWERED_FLIGHT:
            checkApogee();
            break;

        case BALLISTIC_DESCENT:
            checkChuteStop();
            break;

        case CHUTE_DESCENT:
            checkLanded();
            break;

        case GROUND_SAFE:
            showSafe();
            break;

        case ABORT:
            abortFlight();
            break;

        case STATIC_ABORT:
            abortFlight();
            break;
        
        case STATIC_FIRE:
            break;
    }

    logData();
}
