void tun(){

if(analogRead(pitchpin) > 1010){tune = 19;}
     if(analogRead(pitchpin) <= 1010 && analogRead(pitchpin) > 980){tune = 18;}
     if(analogRead(pitchpin) <= 980 && analogRead(pitchpin) > 870){tune = 17;}
     if(analogRead(pitchpin) <= 870 && analogRead(pitchpin) > 760){tune = 16;}
     if(analogRead(pitchpin) <= 760 && analogRead(pitchpin) > 600){tune = 15;}
     if(analogRead(pitchpin) <= 600 && analogRead(pitchpin) > 490){tune = 14;}
     if(analogRead(pitchpin) <= 490 && analogRead(pitchpin) > 375){tune = 13;}
     if(analogRead(pitchpin) <= 375 && analogRead(pitchpin) > 255){tune = 12;}
     if(analogRead(pitchpin) <= 255 && analogRead(pitchpin) > 117){tune = 11;}
     if(analogRead(pitchpin) <= 117 && analogRead(pitchpin) > 50){tune = 10;}
     if(analogRead(pitchpin) <= 50 && analogRead(pitchpin) > 10){tune = 9;}
     if(analogRead(pitchpin) <= 10){tune = 8;}
}
