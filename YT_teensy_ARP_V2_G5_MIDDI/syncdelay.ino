void syncdelay(){
  if(u == 0 && beat==0 ){
    presynctime = synctime;
    u = 1;
  }

  if(u == 1 && beat == 4 ){
    synctempo = synctime-presynctime;
    //if(synctempo<10){synctempo=10;}
    u = 0;
  }
   bpm = 60000/synctempo;
   //tap led light
   if(ledtime-preledtime>synctempo){
      preledtime=ledtime;
      pixels0.setPixelColor(0, pixels0.Color(100,100,100));  
   }else{
       pixels0.setPixelColor(0, pixels0.Color(0,0,0));  
   }
    Serial.println( bpm);
}
