# twitter

### rxcpp example of live twitter analysis

[![president-elect](https://img.youtube.com/vi/QFcy-jQpvBg/0.jpg)](https://www.youtube.com/edit?video_id=QFcy-jQpvBg)

This project was inspired by a [talk](https://blog.niallconnaughton.com/2016/10/25/ndc-sydney-talk/) by @nconnaughton - [github](https://github.com/NiallConnaughton/rx-realtime-twitter), [twitter](https://twitter.com/nconnaughton) 

The goal for this app is to show rxcpp usage and explore machine learning on live data.

This app demonstrates how multi-thread code can be written using rxcpp to hide all the primitives. thread, mutex, etc.. are all hidden behind the algorithms.

CMake is used for the build. There are dependencies on several libraries. (curl, oauth, sdl2, opengl, GLEW, nlohmann/json, Range-v3)

This project has only been built and tested on OS X, but it should be portable to other environments.

Here is a video of an older version running during the seahawks vs eagales game
[![baldwin pass to wilson](https://img.youtube.com/vi/QkvCzShHyVU/0.jpg)](https://www.youtube.com/watch?v=QkvCzShHyVU)

I also uploaded a video of an even older version of the app running on election night 2016
[![election 2016 live twitter](https://img.youtube.com/vi/ewvW4fYE4aQ/0.jpg)](https://www.youtube.com/watch?v=ewvW4fYE4aQ)
