void effect(){
  //envelope
  int att =analogRead(attackpin)/2;
  int rel =analogRead(releasepin);
  envelope1.attack(att);
  envelope1.decay(rel);
//filter
  float cut = analogRead(cutpin);
  cut = map (cut,0,1023,200,7000); 
  float res = analogRead(reverbpin);
  res = map (res,0,1023,0,1);
  biquad1.setLowpass(0, cut,1);

//delay 
    delaytime = 60000/bpm;
    if(delaytime>12000){
      delaytime = 12000;
      }
    delay1.delay(0,delaytime*3/8);
   fb = analogRead(looppin);
   if(abs(prefb-fb)>20){
    prefb = fb;
   feedback = prefb;
   feedback = map (feedback,0,1023,0,1);
   }
   mixer1.gain(2,feedback);//delay feedback

//main volume
  float master = analogRead(mainvolumepin);
  master = map (master,0,1023,0,0.9);
  sgtl5000_1.volume(master);

}
