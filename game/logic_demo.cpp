//the demo logic takes care of moving the camera across the landscape to showcase the engine

void logic_demo() {

    //start of demo
    if (demo_progress == 0) {
        //in the city center
        camera_position[0] = -35;
        camera_position[1] = 4;
        camera_position[2] = -40;
        pitch = 0;
        yaw = 0;
    
    //move to city center
    } else if (demo_progress < 1500) {
        camera_position[2] += 0.03;

    //move to pagoda
    } else if (demo_progress == 1500) {
        //camera_position[0] = -35;
        //camera_position[1] = 4;
        camera_position[2] = 40;
        yaw = PI;

    //from pagoda to city center
    } else if (demo_progress < 2500) {
        camera_position[2] -= 0.03;

    //move to city center facing shop
    } else if (demo_progress == 2500) {
        //camera_position[0] = -35;
        //camera_position[1] = 4;
        camera_position[2] = 5;
        yaw = PI / 2;

    //from city center to outskirts
    } else if (demo_progress < 4500) {
        camera_position[0] += 0.03;

    //down in the grass of the outskirts
    } else if (demo_progress == 4500) {
        camera_position[0] = 35;
        camera_position[1] = 1.8;
        camera_position[2] = -40;
        yaw = 0;

    //through the outskirts
    } else if (demo_progress < 7000) {
        camera_position[2] += 0.03;
    }


    update_camera();
    demo_progress++;
    if (demo_progress >= 7000) {
        demo_progress = 0;
    }
}