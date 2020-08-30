void Oct(){
  if(digitalRead(octrandom)==HIGH){
if(abs(analogRead(octavepin)-lastoctnum) > 3){ 
     if(analogRead(octavepin)<=1023 && analogRead(octavepin)>=990){octnum = 5;}
else if(analogRead(octavepin)< 990 && analogRead(octavepin)>=770){octnum = 4;}
else if(analogRead(octavepin)< 770 && analogRead(octavepin)>=520){octnum = 3;}
else if(analogRead(octavepin)< 520 && analogRead(octavepin)>= 235){octnum = 2;}
else if(analogRead(octavepin)< 235  && analogRead(octavepin)>= 70 ){octnum = 1;}
else if(analogRead(octavepin)< 70  && analogRead(octavepin)>= 0 ){octnum = 0;}
lastoctnum = analogRead(octavepin); 
   }
  }else if(digitalRead(octrandom)==LOW){
    if(abs(analogRead(octavepin)-lastoctnum) > 3){ 
     if(analogRead(octavepin)<=1023 && analogRead(octavepin)>=770){octram = 2;}
else if(analogRead(octavepin)< 770 && analogRead(octavepin)>=235){octram = 1;}
else if(analogRead(octavepin)< 235  && analogRead(octavepin)>= 0 ){octram = 0;}
lastoctram = analogRead(octavepin); 
   }
   octnum = random(0+octram,3+octram);
  }
}
