# twitter
rxcpp example of live twitter analysis

This project was inspired by a [talk](https://blog.niallconnaughton.com/2016/10/25/ndc-sydney-talk/) by @nconnaughton - [github](https://github.com/NiallConnaughton/rx-realtime-twitter), [twitter](https://twitter.com/nconnaughton) 

The goal for this app is to show rxcpp usage and explore machine learning on live data.

This app demonstrates how multi-thread code can be written using rxcpp to hide all the primitives. thread, mutex, etc.. are all hidden.

CMAKE is used for the build. There are depenedcies on several libraries. (curl, oauth, sdl2, opengl)

This project has only been built and tested on OS X.

There are several shortcuts in place - error handling and retry and completion handling have all gotten short-shrift.

