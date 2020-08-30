#include <Bounce.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <Adafruit_NeoPixel.h>


// GUItool: begin automatically generated code
AudioSynthWaveform       waveform1;      //xy=1352.5000228881836,2704.5832920074463
AudioEffectEnvelope      envelope1;      //xy=1539.0000228881836,2704.0832920074463
AudioFilterBiquad        biquad1;        //xy=1698.7500228881836,2642.5000410079956
AudioEffectDelay         delay1;         //xy=1933.7500305175781,2985.0000438690186
AudioMixer4              mixer1;         //xy=1940.0000286102295,2798.75004196167
AudioOutputI2S           i2s;            //xy=2231.250030517578,2797.5832962989807
AudioConnection          patchCord1(waveform1, envelope1);
AudioConnection          patchCord2(envelope1, biquad1);
AudioConnection          patchCord3(biquad1, 0, mixer1, 0);
AudioConnection          patchCord4(delay1, 0, mixer1, 2);
AudioConnection          patchCord5(mixer1, delay1);
AudioConnection          patchCord6(mixer1, 0, i2s, 0);
AudioConnection          patchCord7(mixer1, 0, i2s, 1);
AudioControlSGTL5000     sgtl5000_1;     //xy=2233.7500343322754,2843.5832958221436
// GUItool: end automatically generated code


MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
const int channel = 1;

Bounce buttonTap = Bounce(40, 0);

int s0 = 0; //button values
int s1 = 0; //button values
int s2 = 0; //button values
int s3 = 0; //button values
int s4 = 0; //button values
int s5 = 0; //button values
int s6 = 0; //button values
int s7 = 0; //button values

int r0 = 0; //button values
int r1 = 0; //button values
int r2 = 0; //button values
int r3 = 0; //button values
int r4 = 0; //button values
int r5 = 0; //button values
int r6 = 0; //button values
int r7 = 0; //button values



// midi clock
byte data;
int ppqn = 0;

//pin setting
int waveformpin = A2;
int reverbpin = A13;
int looppin = A14;
int mainvolumepin = A1;
int cutpin = A12;
int attackpin = A10;
int releasepin = A11;
int scalepin = A6;
int pitchpin = A3;
int korgsyncout=26;
int korgsyncin=A16;
int chordbot=5;
int syncbutton=29;
int recbutton = 40;
int runswitch = 27;
int noisebutton = 43;
int tapbutton = 44;
int bpmpot = A15;
int octavepin = A7;
int bpmrandom = 25;
int noterandom = 8;
int octrandom = 24;


//音符设定
int beat = -1;//节拍初始化
int TONE=0;//Arduino输出音色初始化
int Tone;//midi音色
int lastTone;//midi 音色判断
int pitch = 0;
int lastpitch;
int note;
int key=0;//按键调
//八度
int Octave[]={0,12,24,36,48,60};
int oct;
int octnum;
int lastoctnum;
int octram;
int lastoctram;
int lastoct;
int notejudge ;
int starter;
int stopper;
int w = 0;

//judge
int check0 = 0;
int precheck0 = 0;
int check1 = 0;
int precheck1 = 0;
int check2 = 0;
int precheck2 = 0;
int check3 = 0;
int precheck3 = 0;
int check4 = 0;
int precheck4 = 0;
int check5 = 0;
int precheck5 = 0;
int check6 = 0;
int precheck6 = 0;
int check7 = 0;
int precheck7 = 0;
int check8 = 0;
int precheck8 = 0;

//timer setting
int t = 0;
int u = 0;
int bpm = 40;
long tempo = 500;
long synctempo;
long prevMillis = 0;
long interval; 
unsigned long taptime = 0;
unsigned long ledtime = 0;
unsigned long synctime = 0;
long pretaptime = 0;
long preledtime = 0;
long presynctime = 0;
int delaytime = 200;
//delay feedback
int fb = 0;
int prefb = 0;
float feedback = 0 ;
int prebpm = 0;
int lastbpm = 0;
//数值设定
int range;//waveform range
int i;
int m;
int tune;
int State = 0; // 
int p=0;
int sync = 0;
int k= 0;

//音阶模式
int All[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59 };
int Major[]={0,2,4,5,7,9,11,12,14,16,17,19,21,23,24,26,28,29,31,33,35,36,38,40,41,43,45,47,48,50,52,53,55,57,59 };
int Minor[]={0,2,3,5,7,8,10,12,14,15,17,19,20,22,24,26,27,29,31,32,34,36,38,39,41,43,44,48,50,51,53,55,56,58};
int HarmonyMinor[]={0,2,3,5,7,8,11,12,14,15,17,19,20,23,24,26,27,29,31,32,35,36,38,39,41,43,44,47,48,50,51,53,55,56,59};
int MelodyMinor[]={0,2,3,5,7,9,11,12,14,15,17,19,21,23,24,26,27,29,31,33,35,36,38,39,41,43,45,47,48,50,51,53,55,57,59};
int Dorian[]={0,2,3,5,7,9,10,12,14,15,17,19,21,22,24,26,27,29,31,33,34,36,38,39,41,43,45,46,48,50,51,53,55,57,58};
int Lydian[]={0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26,28,30,31,33,35,36,38,40,42,43,45,47,48,50,52,54,55,57,59};
int Gypsy[]={0,3,6,7,8,10,12,15,18,19,20,22,24,27,30,31,32,34,36,39,42,43,44,46,48,51,54,55,56,58};
int Japan[]={0,1,5,7,8,12,13,17,19,20,24,25,29,31,32,36,37,41,43,44,48,49,53,55,56,60,61,65,67,68};
int China[]={0,2,4,7,9,12,14,16,19,21,24,26,28,31,33,36,38,40,43,45,48,50,52,55,57,60,62,64,67,69};
int my1[]={0,1,5,7,8,12,13,17,19,20,24,25,29,31,32,36,37,41,43,44,48,49,53,55,56,60,61,65,67,68};
int my2[]={0,2,4,7,9,12,14,16,19,21,24,26,28,31,33,36,38,40,43,45,48,50,52,55,57,60,62,64,67,69};

int arr_number = 0; 
const int* arr_list[11];
const int *r;


void map_arrays() {          // list of pointers with the addresses of the
// arrays
  arr_list[0] = All;
  arr_list[1] = Major;
  arr_list[2] = Minor;
  arr_list[3] = HarmonyMinor;
  arr_list[4] = Lydian;
  arr_list[5] = Dorian;
  arr_list[6] = Gypsy; 
  arr_list[7] = Japan;
  arr_list[8] = China; 
  arr_list[9] = my1;
  arr_list[10] =my2; 
  
}

uint16_t scale[] = {
20.6,21.83,23.12,24.5,25.96,27.5,29.14,30.87,32.7,34.65,36.71,38.89,
41.2,43.7,46.0,49.0,51.9,55.0,58.3,61.7,65.4,69.3,73.4,77.8,
82.4,87.3,92.5,98.0,103.8,110.0,116.5,123.5,130.8,138.6,146.8,155.6, //0-11
164.8,174.6,185.0,196.0,207.7,220.0,233.1,246.9,261.6,277.2,293.7,311.1,                 //12-23
329.6,349.2,370.0,392.0,415.3,440.0,466.2,493.9,523.3,554.4,587.3,622.3,                 //24-35
659.3,698.5,740.0,784.0,830.6,880.0,932.3,987.8,1047,1109,1175,1245,                     //36-47
1319,1397,1480,1568,1661,1760,1865,1976,2093,2217,2349,2489,                              //48-59
2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,                              //60-71
};

uint16_t midiscale[] = {
28,29,30,31,32,33,34,35,36,37,38,39,
40,41,42,43,44,45,46,47,48,49,50,51,
52,53,54,55,56,57,58,59,60,61,62,63,
64,65,66,67,68,69,70,71,72,73,74,75,
76,77,78,79,80,81,82,83,84,85,86,87,
88,89,90,91,92,93,94,95,96,97,98,99,100,
101,102,103,104,105,106,107,108,109,110,111
};

//led setting

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN0            30
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS0      8
Adafruit_NeoPixel pixels0 = Adafruit_NeoPixel(NUMPIXELS0, PIN0, NEO_GRB + NEO_KHZ800);





void setup() {
MIDI.begin();
AudioMemory(512);
sgtl5000_1.enable();
sgtl5000_1.volume(0.8); 
  waveform1.begin(0.9,0,WAVEFORM_TRIANGLE);
  
  envelope1.attack(0);
  envelope1.hold(0);
  envelope1.decay(0);
  envelope1.sustain(0);
  envelope1.release(100);

// filter
  biquad1.setLowpass(0, 1000, 0.7);
  
//delay
  delay1.delay(0,100);
  delay1.disable(1);
  delay1.disable(2);
  delay1.disable(3);
  delay1.disable(4);
  delay1.disable(5);
  delay1.disable(6);
  delay1.disable(7);

//mixer 1
  mixer1.gain(0,0.5);//filter level
  mixer1.gain(2,0.5);//delay level
  
  map_arrays(); 

  pinMode(recbutton,INPUT_PULLUP);
  pinMode(runswitch,INPUT_PULLUP);
  pinMode(bpmrandom,INPUT_PULLUP);
  pinMode(octrandom,INPUT_PULLUP);
  pinMode(noterandom,INPUT_PULLUP);
  pinMode(runswitch,INPUT_PULLUP);
  pinMode(syncbutton,INPUT_PULLUP);
  pinMode(chordbot,INPUT_PULLUP);
  pinMode(noisebutton,INPUT_PULLUP);
  pinMode(tapbutton,INPUT_PULLUP);
  pinMode(korgsyncout,OUTPUT);//korg sync out
  //mux setting
  pinMode(48, INPUT_PULLUP);
  pinMode(51, INPUT_PULLUP);
  pinMode(52, INPUT_PULLUP);
  pinMode(53, INPUT_PULLUP);
  pinMode(54, INPUT_PULLUP);
  pinMode(55, INPUT_PULLUP);
  pinMode(56, INPUT_PULLUP);
  pinMode(57, INPUT_PULLUP);
  pixels0.begin(); // led start
  pixels0.setPixelColor(0, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(1, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(2, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(3, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(4, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(5, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(6, pixels0.Color(0,5,5)); 
  pixels0.setPixelColor(7, pixels0.Color(0,5,5)); 
  pixels0.show();
  delay(2000);
}

void loop(){
    unsigned long currentMillis = millis();
    taptime = millis();
    ledtime = millis();
    synctime = millis();
  //bpm setting

    interval = 60000/bpm/24;
    
//fuction
    waveformsetting();
    effect();
    readmux();
    

  //run or not   
  if(digitalRead(runswitch)==LOW){//start/stop
      if(digitalRead(bpmrandom)==HIGH){
    bpmmode();
    }else if (digitalRead(bpmrandom)==LOW){
    tapmode();
    }//bpm mode
    //启动声
    if(p==0){ 
       envelope1.noteOn();
       p = 1;
     } 
  if(currentMillis - prevMillis > interval) {
   w = w + 1 ;
   prevMillis = currentMillis;
  // Serial1.write(0xf8);
   if( w > 23){
     w = 0;
    }
   if (w == 0 || w == 6 ||w == 12 || w == 18){
    if(digitalRead(chordbot)==HIGH){
      Sync();
      digitalWrite(korgsyncout,HIGH);
      }else{
      Sync();
      digitalWrite(korgsyncout,HIGH);
      } 
      }else{ 
        digitalWrite(korgsyncout,LOW);
        MIDI.sendNoteOff(midiscale[i], 90, 1); 
      }
    }
     ppqn = 0;//midi clock count
  }

  if(digitalRead(runswitch)==HIGH){ 
      
    if (digitalRead(syncbutton) == LOW ){
       syncdelay();
       if( k == 0 && analogRead(korgsyncin)>200 ){//korgsync 
       Sync();
        digitalWrite(korgsyncout,HIGH);
        k = 1;
       }else if( k == 1 && analogRead(korgsyncin)<=200 ){
        digitalWrite(korgsyncout,LOW);
        k = 0;
       }
     }
  }
  //led showing
   pixels0.show();
  
}
