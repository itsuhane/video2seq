#include <iostream>
#include <boost\program_options.hpp>
#include <opencv2\opencv.hpp>

#define VERSION "1.00"

using namespace std;
using namespace cv;
using namespace boost::program_options;

void banner() {
    cout << "video2seq ver." VERSION " by ljy.swimming@qq.com." << endl << endl;
}

/*
    --input
    --output
    --input-start
    --input-step
    --output-start
    --output-step
    --downscale
    --mode
    --verbose
    --intrinsic-in
    --intrinsic-out
    --undistort
*/

int main(int argc, char *argv[]) {

    string input_path;
    string output_pattern;

    size_t input_start;
    size_t input_step;
    ptrdiff_t input_end;

    size_t output_start;
    size_t output_step;

    size_t downscale;
    string rotate_string;

    string mode_string;
    size_t verbose;

    string intrinsic_in;
    string intrinsic_out;

    bool do_undistort = false;
    bool show_help = false;

    enum RotateMode {
        RM_NONE = 0,
        RM_CW = 1,
        RM_CCW = 2,
        RM_X = 3,
        RM_Y = 4,
        RM_XY = 5,
        RM_DIAG = 6,
        RM_ANTI = 7
    } rotate_mode;

    options_description desc("Allowed options");
    desc.add_options()
        ("input,i", value<string>(&input_path)->required(), "path to input video")
        ("output,o", value<string>(&output_pattern)->required(), "pattern of result path/name, see notes")
        ("input-start,s", value<size_t>(&input_start)->default_value(0), "input frame start")
        ("input-step,S", value<size_t>(&input_step)->default_value(1), "input frame step")
        ("input-end", value<ptrdiff_t>(&input_end)->default_value(-1), "input frame end (inclusive), -1 for the end of video")
        ("output-start", value<size_t>(&output_start)->default_value(0), "result numbering start")
        ("output-step", value<size_t>(&output_step)->default_value(1), "result numbering step")
        ("downscale,d", value<size_t>(&downscale)->default_value(1), "rate of downscale")
        ("rotate,r", value<string>(&rotate_string)->default_value(""), "rotate (or flip) image, accepts none|cw|ccw|x|y|xy|diag|anti")
        ("intrinsic-in,k", value<string>(&intrinsic_in)->default_value(""), "input intrinsic, used for calibration/downscale")
        ("intrinsic-out,K", value<string>(&intrinsic_out)->default_value(""), "output intrinsic, used for downscale, requires --intrinsic-in")
        ("undistort,u", "correct lens distortion, k1 k2 p1 p2 [k3 [k4 k5 k6]], requires --intrinsic-in")
        ("mode", value<string>(&mode_string)->default_value("skip"), "frame searching mode for exporting, see notes")
        ("verbose,v", value<size_t>(&verbose)->default_value(2), "amount of information printed on screen, see notes")
        ("help,h", "show this message")
    ;

    variables_map vm;
    try {
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm);
    }
    catch (...) {
        show_help = true;
    }

    if (vm.count("help")) {
        show_help = true;
    }

    if (show_help) {
        banner();
        cout << "Usage: " << argv[0] << " <options>" << endl;
        cout << desc << endl;
        cout << "Notes:" << endl
            << "--output: a printf format string should be provided to determine the path for every frame exported. There can be ONLY ONE decimal number field (%d) exist in the format string. The number is then filled into this field." << endl
            << "--mode: when searching for next frame, one of the two modes are available: skip and jump. In skip mode, every frame is read and useless frames are skipped, this is useful when --input-step is small. In jump mode, it locates the next frame in video, this is useful when --input-step is large." << endl
            << "--verbose: 0 for a silent output, 1 for basic banner and info, 2(and larger) for live progress." << endl;
        exit(EXIT_SUCCESS);
    }

    // check parameters
    VideoCapture video;
    if (!video.open(input_path)) {
        cout << "Error: cannot open \"" << input_path << "\"" << endl;
        exit(EXIT_FAILURE);
    }
    if (input_step == 0) {
        cout << "Error: input-step cannot be smaller than 1." << endl;
        exit(EXIT_FAILURE);
    }
    if (output_step == 0) {
        cout << "Error: output-step cannot be smaller than 1." << endl;
        exit(EXIT_FAILURE);
    }
    if (input_end >= 0 && (ptrdiff_t)input_start >= input_end) {
        cout << "Warning: input-end is smaller than input-start, no frame will be written." << endl;
        exit(EXIT_SUCCESS);
    }
    if (downscale == 0) {
        cout << "Error: downscale ratio cannot be smaller than 1." << endl;
        exit(EXIT_FAILURE);
    }

    if (rotate_string == "" || rotate_string == "none") {
        rotate_mode = RM_NONE;
    }
    else if (rotate_string == "cw") {
        rotate_mode = RM_CW;
    }
    else if (rotate_string == "ccw") {
        rotate_mode = RM_CCW;
    }
    else if (rotate_string == "x") {
        rotate_mode = RM_X;
    }
    else if (rotate_string == "y") {
        rotate_mode = RM_Y;
    }
    else if (rotate_string == "xy") {
        rotate_mode = RM_XY;
    }
    else if (rotate_string == "diag") {
        rotate_mode = RM_DIAG;
    }
    else if (rotate_string == "anti") {
        rotate_mode = RM_ANTI;
    }
    else {
        rotate_mode = RM_NONE;
    }

    if (mode_string != "skip" && mode_string != "jump") {
        cout << "Error: unknown mode \"" << mode_string << "\"" << endl;
        exit(EXIT_FAILURE);
    }

    if ((vm.count("undistort") || intrinsic_out.size()>0) && intrinsic_in.size()==0) {
        cout << "Error: --intrinsic-out requires --intrinsic-in" << endl;
        exit(EXIT_FAILURE);
    }

    if (vm.count("undistort")) {
        do_undistort = true;
    }

    // get video information
    int width = (int)video.get(CAP_PROP_FRAME_WIDTH);
    int height = (int)video.get(CAP_PROP_FRAME_HEIGHT);
    size_t count = (size_t)video.get(CAP_PROP_FRAME_COUNT);
    size_t save_end = count;
    if (input_end != -1) {
        save_end = (size_t)input_end;
    }

    int save_width = width / (int)downscale;
    int save_height = height / (int)downscale;

    float fx = height, fy = height, cx = width*0.5, cy = height*0.5, k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0, k4 = 0, k5 = 0, k6 = 0;

    if (intrinsic_in.size() > 0) {
        FILE *kin = fopen(intrinsic_in.c_str(), "r");
        if (kin) {
            fscanf(kin, "%f %f %f %f %f %f %f %f %f %f %f %f", &fx, &fy, &cx, &cy, &k1, &k2, &p1, &p2, &k3, &k4, &k5, &k6);
            fclose(kin);

            if (intrinsic_out.size() > 0) {
                FILE *kout = fopen(intrinsic_out.c_str(), "w");
                if (kout) {
                    fprintf(kout, "%f %f %f %f", fx / downscale, fy / downscale, cx / downscale, cy / downscale);
                    fclose(kout);
                }
            }
        }
    }

    if (verbose >= 1) {
        banner();
        cout << "Video size: " << width << "x" << height << endl;
        cout << "Output size: " << save_width << "x" << save_height << endl;
        cout << "Frame number: " << count << endl << endl;
        if (intrinsic_in.size() > 0) {
            cout << "Calibration data:" << endl;
            cout << "fx = " << fx << ", fy = " << fy << ", cx = " << cx << ", cy = " << cy << endl;
            cout << "k1 = " << k1 << ", k2 = " << k2 << ", p1 = " << p1 << ", p2 = " << p2 << ", k3 = " << k3 << ", k4 = " << k4 << ", k5 = " << k5 << ", k6 = " << k6 << endl << endl;
        }
        if (intrinsic_out.size() > 0) {
            cout << "After downscale:" << endl;
            cout << "fx = " << fx / downscale << ", fy = " << fy / downscale << ", cx = " << cx / downscale << ", cy = " << cy / downscale << endl << endl;
        }
    }

    Mat K(3, 3, CV_32FC1);
    K.at<float>(0, 0) = fx;
    K.at<float>(1, 1) = fy;
    K.at<float>(0, 2) = cx;
    K.at<float>(1, 2) = cy;
    K.at<float>(2, 2) = 1;

    vector<float> dist{ k1, k2, p1, p2, k3, k4, k5, k6 };

    Mat tmpframe(width, height, CV_8UC3);
    Mat frame(width, height, CV_8UC3);
    Mat save_frame(save_width, save_height, CV_8UC3);
    char save_name[255];
    if (mode_string == "skip") {
        video.set(CAP_PROP_POS_FRAMES, (double)input_start);
        size_t counter = output_start;
        for (size_t i = input_start; i < count && i <= save_end; ++i) {
            video >> tmpframe;
            if ((i - input_start) % input_step == 0) {
                if (do_undistort) {
                    cv::undistort(tmpframe, frame, K, dist);
                }
                else {
                    frame = tmpframe;
                }

                sprintf_s(save_name, output_pattern.c_str(), counter);
                if (downscale > 1) {
                    resize(frame, save_frame, Size(save_width, save_height));
                    imwrite(save_name, save_frame);
                }
                else {
                    imwrite(save_name, frame);
                }
                if (verbose >= 2) {
                    cout << "\rCurrent frame: " << i;
                }
                counter += output_step;
            }
        }
        cout << endl;
    }
    else {
        size_t counter = output_start;
        for (size_t i = input_start; i < count && i <= save_end; i += input_step) {
            video.set(CAP_PROP_POS_FRAMES, (double)i);
            video >> tmpframe;
            if (do_undistort) {
                cv::undistort(tmpframe, frame, K, dist);
            }
            else {
                frame = tmpframe;
            }
            sprintf_s(save_name, output_pattern.c_str(), counter);
            if (downscale > 1) {
                resize(frame, save_frame, Size(save_width, save_height));
                imwrite(save_name, save_frame);
            }
            else {
                imwrite(save_name, frame);
            }
            if (verbose >= 2) {
                cout << "\rCurrent frame: " << i;
            }
            counter += output_step;
        }
        cout << endl;
    }

    return 0;
}