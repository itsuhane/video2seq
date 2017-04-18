#include <iostream>
#include <boost\program_options.hpp>
#include <opencv2\opencv.hpp>

#define VERSION "1.50"

using namespace std;
using namespace cv;
using namespace boost::program_options;

void banner() {
    cout << "video2seq ver." VERSION " by ljy.swimming@qq.com." << endl;
}

class VideoSeeker {
public:
    bool open(const std::string &path) {
        return video.open(path);
    }

    void set_mode(bool jump = false) {
        this->jump = jump;
    }

    void set_range(size_t first, ptrdiff_t last, size_t step = 1) {
        size_t count = (size_t)video.get(CAP_PROP_FRAME_COUNT);
        this->first = first;
        if (last < 0) {
            this->last = count;
        }
        else {
            this->last = min((size_t)last, count);
        }
        this->step = step;
        this->curr_index = this->next_index = this->first;
    }

    bool has_next() const {
        return next_index <= this->last;
    }

    size_t index() const {
        return next_index;
    }

    Mat next() {
        if (has_next()) {
            Mat result;
            if (jump) {
                curr_index = next_index;
                video.set(CAP_PROP_POS_FRAMES, (double)curr_index);
                video >> result;
            }
            else {
                while (curr_index <= next_index) {
                    video >> result;
                    curr_index++;
                }
            }
            next_index += step;
            return result;
        }
        else {
            return Mat();
        }
    }

    int width() {
        return (int)video.get(CAP_PROP_FRAME_WIDTH);
    }

    int height() {
        return (int)video.get(CAP_PROP_FRAME_HEIGHT);
    }

    size_t total() {
        return last;
    }
private:
    VideoCapture video;
    bool jump = false;
    size_t first = 0;
    size_t last = size_t(-1);
    size_t step = 1;
    size_t curr_index = 0;
    size_t next_index = 0;
};

enum RotateMode {
    RM_NONE = 0,
    RM_CW = 1,
    RM_CCW = 2,
    RM_X = 3,
    RM_Y = 4,
    RM_XY = 5,
    RM_DIAG = 6,
    RM_ANTI = 7
};

Mat transform_image(const Mat &mat, RotateMode rm) {
    Mat result = mat;
    switch (rm) {
    default:
    case RM_NONE:
        break;
    case RM_CW:
        transpose(result, result);
        flip(result, result, 1);
        break;
    case RM_CCW:
        transpose(result, result);
        flip(result, result, 0);
        break;
    case RM_X:
        flip(result, result, 1);
        break;
    case RM_Y:
        flip(result, result, 0);
        break;
    case RM_XY:
        flip(result, result, -1);
        break;
    case RM_DIAG:
        transpose(result, result);
        break;
    case RM_ANTI:
        transpose(result, result);
        flip(result, result, -1);
        break;
    }
    return result;
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

    RotateMode rotate_mode;

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
    VideoSeeker video;
    // VideoCapture video;
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
    video.set_range(input_start, input_end, input_step);

    if (downscale == 0) {
        cout << "Error: downscale ratio cannot be smaller than 1." << endl;
        exit(EXIT_FAILURE);
    }

    transform(rotate_string.begin(), rotate_string.end(), rotate_string.begin(), tolower);
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
        cout << "Error: unknown transform \"" << rotate_string << "\"" << endl;
        exit(EXIT_FAILURE);
    }

    transform(mode_string.begin(), mode_string.end(), mode_string.begin(), tolower);
    if (mode_string != "skip" && mode_string != "jump") {
        cout << "Error: unknown mode \"" << mode_string << "\"" << endl;
        exit(EXIT_FAILURE);
    }

    if (mode_string == "jump") {
        video.set_mode(true);
    }

    if ((vm.count("undistort") || intrinsic_out.size()>0) && intrinsic_in.size()==0) {
        cout << "Error: --intrinsic-out requires --intrinsic-in" << endl;
        exit(EXIT_FAILURE);
    }

    if (vm.count("undistort")) {
        do_undistort = true;
    }

    // get video information
    int width = video.width();
    int height = video.height();

    int save_width = width / (int)downscale;
    int save_height = height / (int)downscale;

    float fx = (float)height, fy = (float)height, cx = width*0.5f, cy = height*0.5f, k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0, k4 = 0, k5 = 0, k6 = 0;

    float fx_new = fx, fy_new = fy, cx_new = cx, cy_new = cy;

    if (intrinsic_in.size() > 0) {
        FILE *kin = fopen(intrinsic_in.c_str(), "r");
        if (kin) {
            fscanf(kin, "%f %f %f %f %f %f %f %f %f %f %f %f", &fx, &fy, &cx, &cy, &k1, &k2, &p1, &p2, &k3, &k4, &k5, &k6);
            fclose(kin);

            switch (rotate_mode) {
            default:
            case RM_NONE:
                fx_new = fx / downscale;
                fy_new = fy / downscale;
                cx_new = cx / downscale;
                cy_new = cy / downscale;
                //fprintf(kout, "%f %f %f %f", fx / downscale, fy / downscale, cx / downscale, cy / downscale);
                break;
            case RM_CW:  // fx <-> fy, cx <- h-cy, cy <- cx
                fx_new = fy / downscale;
                fy_new = fx / downscale;
                cx_new = (height - cy) / downscale;
                cy_new = cx / downscale;
                //fprintf(kout, "%f %f %f %f", fy / downscale, fx / downscale, (height - cy) / downscale, cx / downscale);
                break;
            case RM_CCW: // fx <-> fy, cx <- cy, cy<- w-cx
                fx_new = fy / downscale;
                fy_new = fx / downscale;
                cx_new = cy / downscale;
                cy_new = (width - cx) / downscale;
                //fprintf(kout, "%f %f %f %f", fy / downscale, fx / downscale, cy / downscale, (width - cx) / downscale);
                break;
            case RM_X: // cx <- w-cx
                fx_new = fx / downscale;
                fy_new = fy / downscale;
                cx_new = (width-cx) / downscale;
                cy_new = cy / downscale;
                //fprintf(kout, "%f %f %f %f", fx / downscale, fy / downscale, (width - cx) / downscale, cy / downscale);
                break;
            case RM_Y: // cy <- h-cy
                fx_new = fx / downscale;
                fy_new = fy / downscale;
                cx_new = cx / downscale;
                cy_new = (height-cy) / downscale;
                //fprintf(kout, "%f %f %f %f", fx / downscale, fy / downscale, cx / downscale, (height - cy) / downscale);
                break;
            case RM_XY:// cx <- w-cx, cy <- h-cy
                fx_new = fx / downscale;
                fy_new = fy / downscale;
                cx_new = (width-cx) / downscale;
                cy_new = (height-cy) / downscale;
                //fprintf(kout, "%f %f %f %f", fx / downscale, fy / downscale, (width - cx) / downscale, (height - cy) / downscale);
                break;
            case RM_DIAG: // fx <-> fy, cx <-> cy
                fx_new = fy / downscale;
                fy_new = fx / downscale;
                cx_new = cy / downscale;
                cy_new = cx / downscale;
                //fprintf(kout, "%f %f %f %f", fy / downscale, fx / downscale, cy / downscale, cx / downscale);
                break;
            case RM_ANTI: // fx <-> fy, cx <- h-cy, cy <- w-cx
                fx_new = fy / downscale;
                fy_new = fx / downscale;
                cx_new = (height-cy) / downscale;
                cy_new = (width-cx) / downscale;
                //fprintf(kout, "%f %f %f %f", fy / downscale, fx / downscale, (height - cy) / downscale, (width - cx) / downscale);
                break;
            }

            if (intrinsic_out.size() > 0) {
                FILE *kout = fopen(intrinsic_out.c_str(), "w");
                if (kout) {
                    fprintf(kout, "%f %f %f %f", fx_new, fy_new, cx_new, cy_new);
                    fclose(kout);
                }
            }
        }
    }

    if (verbose >= 1) {
        banner();
        cout << "Video size: " << width << "x" << height << endl;
        cout << "Output size: " << save_width << "x" << save_height << endl;
        cout << "Last frame: " << video.total() << endl;
        //cout << "Frame number: " << count << endl << endl;
        if (intrinsic_in.size() > 0) {
            cout << "Calibration data:" << endl;
            cout << "fx = " << fx << ", fy = " << fy << ", cx = " << cx << ", cy = " << cy << endl;
            cout << "k1 = " << k1 << ", k2 = " << k2 << ", p1 = " << p1 << ", p2 = " << p2 << ", k3 = " << k3 << ", k4 = " << k4 << ", k5 = " << k5 << ", k6 = " << k6 << endl << endl;
        }
        if (intrinsic_out.size() > 0) {
            cout << "After transformation:" << endl;
            cout << "fx = " << fx_new << ", fy = " << fy_new << ", cx = " << cx_new << ", cy = " << cy_new << endl << endl;
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

    size_t counter = output_start;
    while (video.has_next()) {
        size_t i = video.index();
        tmpframe = video.next();
        if (do_undistort) {
            cv::undistort(tmpframe, frame, K, dist);
        }
        else {
            frame = tmpframe;
        }
        sprintf_s(save_name, output_pattern.c_str(), counter);
        if (downscale > 1) {
            resize(frame, save_frame, Size(save_width, save_height));
            imwrite(save_name, transform_image(save_frame, rotate_mode));
        }
        else {
            imwrite(save_name, transform_image(frame, rotate_mode));
        }
        if (verbose >= 2) {
            cout << "\rCurrent frame: " << i;
        }
        counter += output_step;
    }
    cout << endl;

    return 0;
}
