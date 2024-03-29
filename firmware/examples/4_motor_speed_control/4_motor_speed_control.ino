/*
 * A motor speed controller example for 4 motors at the same time
 */

/******** INCLUDE *********/

#include <SparkFun_TB6612.h> // TB6612FNG (motor driver / H-bridge chip) library to control the motors

/* There are only two external interrupt pins, INT0 and INT1, and they are mapped to Arduino pins 2 and 3,
 * however on the Osoyoo shield the encoders are wired to pins 2 and 4. Therefore the use of a special library
 * to handle interruptions on any desired pin is needed.
 * According to their documentation the PinChangeInt library can handle the Arduino's pin change interrupts
 * as quickly, easily, and reasonably as possible.
 */

#include <PinChangeInt.h>
#include <PID_v1.h> // PID based control library to achieve motor speed setpoint as fast and smooth as possible

#include <encoder_handler.h> // helper encoder library

/******** DEFINE *********/

/* motors
 *
 *   forward
 * MFL -- MFR
 *  |      |
 *  |      |
 * MRL -- MRR
 *    back
 */

#define IN1_MRR 8   // MRR: Motor rear right
#define IN2_MRR 10
#define PWM_MRR 9   // ENA: enable/disable H bridge (used as PWM to control motor speed)

#define IN1_MRL 12  // MRL: Motor rear left
#define IN2_MRL 13
#define PWM_MRL 11

#define IN1_MFR 5   // MFR: Motor front right
#define IN2_MFR 7
#define PWM_MFR 6

#define IN1_MFL 4   // MFL: Motor front left
#define IN2_MFL 2
#define PWM_MFL 3

// encoders

#define hall_sensor_RR1 A15 // RR
#define hall_sensor_RR2 A14 // RR

#define hall_sensor_RL1 A12 // RL
#define hall_sensor_RL2 A13 // RL

#define hall_sensor_FR1 A9  // FR
#define hall_sensor_FR2 A11 // FR

#define hall_sensor_FL1 A10 // FL
#define hall_sensor_FL2 A8  // FL

#define pulses_per_revolution 450

/******** GLOBALS *********/

// motors

// these constants are used to allow you to make your motor configuration 
// line up with function names like forward.  Value can be 1 or -1
const int motor_offset_rr = -1;
const int motor_offset_rl = -1;
const int motor_offset_fr = 1;
const int motor_offset_fl = -1;

// Initializing motors.  The library will allow you to initialize as many
// motors as you have memory for.  If you are using functions like forward
// that take 2 motors as arguments you can either write new functions or
// call the function more than once.
Motor motor_rr = Motor(IN1_MRR, IN2_MRR, PWM_MRR, motor_offset_rr);
Motor motor_rl = Motor(IN1_MRL, IN2_MRL, PWM_MRL, motor_offset_rl);
Motor motor_fr = Motor(IN1_MFR, IN2_MFR, PWM_MFR, motor_offset_fr);
Motor motor_fl = Motor(IN1_MFL, IN2_MFL, PWM_MFL, motor_offset_fl);

// encoder handlers
// NOTE: most encoder pin setup is done in constructor
EncoderHandler encoder_rr = EncoderHandler(hall_sensor_RR1, hall_sensor_RR2);
EncoderHandler encoder_rl = EncoderHandler(hall_sensor_RL1, hall_sensor_RL2);
EncoderHandler encoder_fr = EncoderHandler(hall_sensor_FR1, hall_sensor_FR2);
EncoderHandler encoder_fl = EncoderHandler(hall_sensor_FL1, hall_sensor_FL2);

// measure elapsed time
unsigned long last_time;

// PID

// PID documentation available under:
//    http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/

double setpoint = 3.0;
double rr_speed_sensor = 0.0;
double rl_speed_sensor = 0.0;
double fr_speed_sensor = 0.0;
double fl_speed_sensor = 0.0;
double rr_double_pid_output = 0.0;
double rl_double_pid_output = 0.0;
double fr_double_pid_output = 0.0;
double fl_double_pid_output = 0.0;

// Specify the links and initial tuning parameters
double kp = 0.045;
double ki = 150.0;
double kd = 0.1;
int ctrl_delay = 49;

PID rr_pid(&rr_speed_sensor, &rr_double_pid_output, &setpoint, kp, ki, kd, DIRECT);
PID rl_pid(&rl_speed_sensor, &rl_double_pid_output, &setpoint, kp, ki, kd, DIRECT);
PID fr_pid(&fr_speed_sensor, &fr_double_pid_output, &setpoint, kp, ki, kd, DIRECT);
PID fl_pid(&fl_speed_sensor, &fl_double_pid_output, &setpoint, kp, ki, kd, DIRECT);

/******** SETUP *********/

void setup()
{
  // initialize serial port at 9600 baud rate
  Serial.begin(9600);

  // encoder pin configuration
  // setup interrupt callback function, gets executed upon pin state change
  auto rr1_pin_change = [] () { encoder_rr.encoder_state_change(); }; // C++ 11 lambda functions
  auto rl1_pin_change = [] () { encoder_rl.encoder_state_change(); };
  auto fr1_pin_change = [] () { encoder_fr.encoder_state_change(); };
  auto fl1_pin_change = [] () { encoder_fl.encoder_state_change(); };
  PCintPort::attachInterrupt(hall_sensor_RR1, rr1_pin_change, CHANGE);
  PCintPort::attachInterrupt(hall_sensor_RL1, rl1_pin_change, CHANGE);
  PCintPort::attachInterrupt(hall_sensor_FR1, fr1_pin_change, CHANGE);
  PCintPort::attachInterrupt(hall_sensor_FL1, fl1_pin_change, CHANGE);

  // initialize variable to measure speed
  last_time = millis();

  // PID controller

  // limit the output of the PID controller between 0 and 255
  rr_pid.SetOutputLimits(0, 255);
  rl_pid.SetOutputLimits(0, 255);
  fr_pid.SetOutputLimits(0, 255);
  fl_pid.SetOutputLimits(0, 255);

  // turn the PID on and rise flag for "auto" mode
  rr_pid.SetMode(AUTOMATIC);
  rl_pid.SetMode(AUTOMATIC);
  fr_pid.SetMode(AUTOMATIC);
  fl_pid.SetMode(AUTOMATIC);

  // sets the period, in Milliseconds, at which the calculation is performed
  rr_pid.SetSampleTime(ctrl_delay);
  rl_pid.SetSampleTime(ctrl_delay);
  fr_pid.SetSampleTime(ctrl_delay);
  fl_pid.SetSampleTime(ctrl_delay);

  // not sure if this is really needed
  delay(300);
  Serial.println("setup complete");
}

float measureSpeed(double &rr_speed, double &rl_speed, double &fr_speed, double &fl_speed)
{
  float delta_pulses_rr = float(encoder_rr.get_encoder_count());
  float delta_pulses_rl = float(encoder_rl.get_encoder_count());
  float delta_pulses_fr = float(encoder_fr.get_encoder_count());
  float delta_pulses_fl = float(encoder_fl.get_encoder_count());

  unsigned long current_time = millis();

  float delta_time = float(current_time - last_time);

  // get time for next cycle
  last_time = current_time;

  // reset count
  encoder_rr.reset_encoder_count();
  encoder_rl.reset_encoder_count();
  encoder_fr.reset_encoder_count();
  encoder_fl.reset_encoder_count();

  /*  speed conversion from : [pulses] / [ms]    to    [rad] / [sec]
   *  
   *  delta_pulses_right [pulses]    1000 [ms]               2 Pi [rad]
   *  --------------------------- * ----------- * ---------------------------------
   *       delta_time [ms]             1 [sec]     pulses_per_revolution [pulses]
   */

  // return value is in : rad/sec
  // 1000 * 2 * pi = 6283.1853071
  rr_speed = double((delta_pulses_rr * 6283.1853071) / ( delta_time * pulses_per_revolution));
  rl_speed = double((delta_pulses_rl * 6283.1853071) / ( delta_time * pulses_per_revolution));
  fr_speed = double((delta_pulses_fr * 6283.1853071) / ( delta_time * pulses_per_revolution));
  fl_speed = double((delta_pulses_fl * 6283.1853071) / ( delta_time * pulses_per_revolution));
}

/******** LOOP *********/

void loop()
{
  // update PID input
  measureSpeed(rr_speed_sensor, rl_speed_sensor, fr_speed_sensor, fl_speed_sensor);

  // calculate required PWM to match the desired speed
  rr_pid.Compute();
  rl_pid.Compute();
  fr_pid.Compute();
  fl_pid.Compute();

  // cast PID double precision floating point output to int
  int int_rr_pid_output = int(rr_double_pid_output);
  int int_rl_pid_output = int(rl_double_pid_output);
  int int_fr_pid_output = int(fr_double_pid_output);
  int int_fl_pid_output = int(fl_double_pid_output);

  // send PID output to motor
  motor_rr.drive(int_rr_pid_output);
  motor_rl.drive(int_rl_pid_output);
  motor_fr.drive(int_fr_pid_output);
  motor_fl.drive(int_fl_pid_output);

  // debug info (uncomment at will)
  // Serial.println("setpoint :");
  // Serial.println(setpoint);
  // Serial.println("rr_speed_sensor :");
  Serial.println(rr_speed_sensor);
  // Serial.println("rr PID output :");
  // Serial.println(int_rr_pid_output);

  delay(ctrl_delay);
}
