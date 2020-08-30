void waveformsetting(){
//声音设定
if(analogRead(waveformpin)<1023 && analogRead(waveformpin)>=980){ range = 5;}
if(analogRead(waveformpin)<980 && analogRead(waveformpin)>=780){ range = 4;}
if(analogRead(waveformpin)< 780 && analogRead(waveformpin)>=580){ range = 3;}
if(analogRead(waveformpin)< 580 && analogRead(waveformpin)>=250){ range = 2;}
if(analogRead(waveformpin)< 250 && analogRead(waveformpin)>=30){ range = 1;}
if(analogRead(waveformpin)< 30 ){ range = 0;}
switch(range){
  case 0:
  waveform1.begin( WAVEFORM_SINE);
  break;
  case 1:
  waveform1.begin( WAVEFORM_TRIANGLE);
  break;
  case 2:
  waveform1.begin( WAVEFORM_SAWTOOTH);
  break;
  case 3:
  waveform1.begin( WAVEFORM_SQUARE);
  break;
  case 4:
  waveform1.begin( WAVEFORM_PULSE);
  break;
  case 5:
  waveform1.begin( WAVEFORM_SAMPLE_HOLD);
  break;
}
}
