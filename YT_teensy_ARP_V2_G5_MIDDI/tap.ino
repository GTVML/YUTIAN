void tapmode(){
 //tap button
  buttonTap.update();
  if(t == 0 && buttonTap.fallingEdge()){
    pretaptime = taptime;
    t = 1;
  }

  if(t == 1 && buttonTap.fallingEdge()&&taptime-pretaptime>0){
    tempo = taptime-pretaptime;
    if(tempo<10){tempo=10;}
    t = 0;
  }
   bpm = 60000/tempo;
   //tap led light
   if(ledtime-preledtime>tempo){
      preledtime=ledtime;
      pixels0.setPixelColor(0, pixels0.Color(100,100,100));  
   }else{
       pixels0.setPixelColor(0, pixels0.Color(0,0,0));  
   }
}
