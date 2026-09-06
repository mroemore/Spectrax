- removing certain steps causes a segfault. I removed steps 0,1 and 2 from pattern 00, it seems like step 1 was fine but removing step 0 or 2 caused uninit memory to be read:
    `^[[1;2C^[[1;2Cjj^[[1;2Cediting...editing...blank! setting: 1568160744 32766Grabbing step: 1568160744 32766`
    later steps also cause a crash, maybe only 0 and 1 don't. needs investigation either way.
- there seems to be a faint clicking sound when instruments start and stop playing.
- the scheduling of notes is a bit wonky sounding, even with swing at 1:1, especially at the boundary of patterns, and even more so at the beginning of playback.
- when you first open the app and start playing, sometimes the first pattern plays twice.

