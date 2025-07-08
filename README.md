CLONE THE REPO
-> git clone https://github.com/prashannaChand/SangeetAdda


STEPS TO RUN THE CODE

1)ADD MUSIC (.wav extension) IN YOUR "playlist" FOLDER IN SERVER FOLDER

2)COMPILE THE CODE IN TERMINAL USING THE COMMANDS   
 ->CD server/
    gcc -o server mainserver.c -lws2_32
 ->CD client/
 gcc -o client mainclient.c -lws2_32 -lwinmm

 3)RUN THE SERVER FIRST THEN CLIENT
  -> CD server
        ./server
    
  -> CD client 
        ./client

 4) ADD NEW CLIENTS AS PER THE NEED FROM client DIRECTORY
    ->./client
 
 5) CHOOSE THE SONG AND PLAY IT / PAUSE IT ETC 