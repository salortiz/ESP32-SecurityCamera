/**************************************************************************************************
 *
 *                            Send images via PHP - 02Jan22
 *
 *       based on code from: https://RandomNerdTutorials.com/esp32-cam-post-image-photo-server/
 *
 *                        see bottom of php.h for the scripts to use
 *
 **************************************************************************************************/


String postImage(WiFiClient, uint16_t*, size_t, String);

//  ----------------------  s e t t i n g s --------------------------
const String PostServerPath = "/upload";         // the php script file location
//  ------------------------------------------------------------------


// ----------------------------------------------------------------
//                        send photo via POST
// ----------------------------------------------------------------
// pass image frame buffer pointer, length, file name to use

String postImage(WiFiClient client, uint8_t* fbBuf, size_t fbLen, String fName = "cwm") {
    String getAll;
    String getBody;
    long startTime = millis();
    long startTimer = 0;
    if (serialDebug)
        Serial.println("Connecting to server: " + PostServerName + ":" + PostServerPort);
    if (client.connected() || client.connect(PostServerName.c_str(), PostServerPort)) {
#define LBOUND "1234567890009876564321"
        if (serialDebug)
            Serial.println("Connection successful");
        String head = "--"LBOUND"\r\n"
            "Content-Disposition: form-data; name=\"imageFile\"; filename=\"" + fName + "\"\r\n"
            "Content-Type: image/jpeg\r\n\r\n";
        String tail = "\r\n--"LBOUND"--\r\n";

        uint16_t imageLen = fbLen;
        uint16_t extraLen = head.length() + tail.length();
        uint16_t totalLen = imageLen + extraLen;

        client.println("POST " + PostServerPath + " HTTP/1.1");
        client.println("Host: " + PostServerName);
        client.println("Content-Length: " + String(totalLen));
        client.println("Content-Type: multipart/form-data; boundary="LBOUND);
        client.println();
        client.print(head);
#undef LBOUND
        // send image from buffer
        for (size_t n=0; n<fbLen; n=n+1024) {
            if (n+1024 < fbLen) {
                client.write(fbBuf, 1024);
                fbBuf += 1024;
            }
            else if (fbLen%1024>0) {
                size_t remainder = fbLen%1024;
                client.write(fbBuf, remainder);
            }
        }
        client.print(tail);

        int timoutTimer = 5000;
        startTimer = millis();
        boolean state = false;

        // receive reply from server
        while ((startTimer + timoutTimer) > millis()) {
            while (client.available()) {
                char c = client.read();
                if (c == '\n') {
                    if (getAll.length()==0) state=true;
                    getAll = "";
                } else if (c != '\r') {
                  getAll += String(c);
                }
                if (state==true) getBody += String(c);
                startTimer = millis();
            }
            if (getBody.length()>0) break;
            if (serialDebug) Serial.print(".");
            delay(30);
        }
        if (serialDebug) {
            Serial.println();
            Serial.println(getBody);
        }
    } else {
        getBody = "POST error-Connection to " + PostServerName +  " failed";
        if (serialDebug) Serial.println(getBody);
    }

    // log result
    if (getBody.indexOf("has been uploaded") == -1) {
        log_system_message("Error sending image '" + fName + "' via POST");
    } else {
        log_system_message("Image '" + fName + "' sent via POST in " + String(startTimer-startTime) + "ms");
    }

    return getBody;
}


/******************************
--------------------------------------------------------------------------------------
                        PHP scripts for use with php.h
--------------------------------------------------------------------------------------


HTML/PHP script to display all images in the folder:


<html>
    <head>
       <title>ESP32Cam Images</title>
    </head>
    <body>
         <center><H1>Images</H1>
         <?php
          // show all images in folder
             $images = glob("*.jpg");
             foreach($images as $image) {
                // echo $image.' <br><img width="640" src="'.$image.'" /><br><br>\n';   // display image
                echo "<br><a href='./" . $image  . "'>" . $image . "</a>\n";   // insert link to image
             }
          ?>
    </body>
</html>


--------------------------------------------------------------------------------------


PHP script to receive images from the ESP32Cam:


    <?php
    // receive image files from esp32cam - dec21
    // from: https://RandomNerdTutorials.com/esp32-cam-post-image-photo-server/
    // results in file name format:   2021.12.30_21:43:05_esp32-cam.jpg

    $target_dir = "./";
    $datum = mktime(date('H')+0, date('i'), date('s'), date('m'), date('d'), date('y'));
    $target_file = $target_dir . date('Y.m.d_H:i:s_', $datum) . basename($_FILES["imageFile"]["name"]);
    $uploadOk = 1;
    $imageFileType = strtolower(pathinfo($target_file,PATHINFO_EXTENSION));


    // Check if image file is a actual image or fake image
    if(isset($_POST["submit"])) {
        $check = getimagesize($_FILES["imageFile"]["tmp_name"]);
        if($check !== false) {
            echo "File is an image - " . $check["mime"] . ".";
            $uploadOk = 1;
        }
        else {
            echo "File is not an image.";
            $uploadOk = 0;
        }
    }

    // Check if file already exists
    if (file_exists($target_file)) {
        echo "Sorry, file already exists.";
        $uploadOk = 0;
    }

    // Check file size
    if ($_FILES["imageFile"]["size"] > 500000) {
        echo "Sorry, your file is too large.";
        $uploadOk = 0;
    }

    // Allow certain file formats
    if($imageFileType != "jpg" && $imageFileType != "png" && $imageFileType != "jpeg"
    && $imageFileType != "gif" ) {
        echo "Sorry, only JPG, JPEG, PNG & GIF files are allowed.";
        $uploadOk = 0;
    }

    // Check if $uploadOk is set to 0 by an error
    if ($uploadOk == 0) {
        echo "Sorry, your file was not uploaded.";
        // if everything is ok, try to upload file
    }
    else {
        if (move_uploaded_file($_FILES["imageFile"]["tmp_name"], $target_file)) {
            echo "The file ". basename( $_FILES["imageFile"]["name"]). " has been uploaded.";
        }
        else {
            echo "Sorry, there was an error uploading your file.";
        }
    }
    ?>


--------------------------------------------------------------------------------------
                                     * end */
