#include <openni2-net-client.h>

int main(int argc, char** argv){

	OpenNI2NetClient client;

	cv::namedWindow("win", cv::WINDOW_AUTOSIZE);

	cv::Mat matThread, mat;
	std::mutex mtx;
	std::atomic_bool bNewMat;
	bNewMat = false;

	client.setCallback([&](const cv::Mat& in){
		mtx.lock();
		in.copyTo(matThread);
		mtx.unlock();
		bNewMat = true;
	});

	while(cv::waitKey(10) != 27){
		if(bNewMat) {
			mtx.lock();
			matThread.convertTo(mat, CV_8U, 255.f / 3000.f);
			mtx.unlock();
			cv::imshow("win", mat);
			bNewMat = false;
		}
	}

	return EXIT_SUCCESS;
}