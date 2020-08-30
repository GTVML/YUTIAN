void bpmmode(){
      lastbpm=analogRead(bpmpot);
  if(abs(prebpm - lastbpm)>30){
    prebpm = lastbpm;
  }
    bpm=prebpm;
    bpm=map(bpm,0,1023,1,300);

       //tap led light
   if(ledtime-preledtime>60000/bpm){
      preledtime=ledtime;
      pixels0.setPixelColor(0, pixels0.Color(100,100,100));  
   }else{
       pixels0.setPixelColor(0, pixels0.Color(0,0,0));  
   }
}
