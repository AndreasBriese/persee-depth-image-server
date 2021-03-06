//
// Created by phwhitfield on 1/24/18.
//

#include <chrono>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

//#include <pcl/compression/organized_pointcloud_conversion.h>

#include "openni2-net-client.h"

//boost::asio::io_service OpenNI2NetClient::ioService;
//bool OpenNI2NetClient::ioServiceRunning = false;

OpenNI2NetClient::OpenNI2NetClient(unsigned int p) : port(p) {
	cloud = Cloud::Ptr(new Cloud());
	start();
}

OpenNI2NetClient::~OpenNI2NetClient() {
	stop();
}


void OpenNI2NetClient::stop() {
	LOGI << "Stop Openni Listener on port " << port;
//	try {
//		if (acceptor) acceptor->close();
//		if (socket) {
//			socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
//			socket->close();
//		}
//		if (ioService) ioService->stop();
//		bKeepRunning = false;
//	} catch (const std::exception &e) {
//		LOGW << e.what();
//		if (ioService) ioService->stop();
//	};
//	acceptor.reset();
//	socket.reset();
//	ioService.reset();

	if(ioService){
		ioService->stop();
	}

	bKeepRunning = false;
	if (thread.joinable()) thread.join();

}


void OpenNI2NetClient::setCallbackCv(const OpenNI2NetClient::CallbackCv &callback) {
	callbackCv = callback;
}

void OpenNI2NetClient::setCallbackPcl(const OpenNI2NetClient::CallbackPcl &callback) {
	callbackPcl = callback;
}

void OpenNI2NetClient::start() {

//	if(!ioServiceRunning){
//		LOGI << "Start openni network io service";
//		ioService.run();
//		ioServiceRunning = true;
//	}

	if (thread.joinable())
		thread.detach();

	thread = std::thread([&] {

		bKeepRunning = true;

		using boost::asio::ip::tcp;

		std::condition_variable cv;
		std::mutex cvMutex;

		ioService = std::make_shared<boost::asio::io_service>();
		socket = std::make_shared<tcp::socket>(*ioService);
		acceptor = std::make_shared<tcp::acceptor>(*ioService, tcp::endpoint(tcp::v4(), port));
		//acceptor->accept(*socket);

		LOGI << "Listening for openni data on port " << port;
//
		std::unique_lock<std::mutex> lk(cvMutex);
		acceptor->async_accept(*socket, [&](const boost::system::error_code& ec_){
			ioService->stop();
		});
		ioService->run();

		LOGI << "Openni connected on port " << port;

		size_t bufferSize = 0;

		int counter = 0;

		std::vector<uint8_t> buffer;
		OpenNI2NetHeader header;
		boost::system::error_code error;

		//
		while (bKeepRunning) {
			auto start = std::chrono::high_resolution_clock::now();

			//size_t amt = 0;

			auto amt = socket->read_some(boost::asio::buffer(&header, sizeof(OpenNI2NetHeader)), error);
//			socket.async_read_some(boost::asio::buffer(&header, sizeof(OpenNI2NetHeader)), [&](const boost::system::error_code& ec_, size_t _amt){
//				amt = _amt;
//				cv.notify_all();
//			});
//
//			if(cv.wait_for(lk, std::chrono::seconds(1)) == std::cv_status::no_timeout){
//				LOGI << "GOT DATA";
//			}else{
//				LOGI << "Read timeout openni client on port: " << port;
//				socket.cancel();
//				continue;
//			}
//			LOGI << "LEAVE";


			if (amt == 0) { continue; };

			buffer.clear();
			buffer.reserve(header.size);

			while (bKeepRunning && buffer.size() < header.size) {
				std::array<unsigned char, 2048> data;
				size_t toRead = data.size();
				if (toRead > header.size - buffer.size()) {
					toRead = header.size - buffer.size();
				}
				amt = socket->read_some(boost::asio::buffer(data, toRead), error);

				buffer.insert(buffer.end(), data.begin(), data.begin() + amt);
			}

			if (!bKeepRunning) break;

			if (buffer.size() != header.size) {
				LOGE << "Wrong buffer size received  " << std::endl;
				continue;
			}

			cv::Mat mat(header.height, header.width, CV_16UC1);

			if (header.jpeg == 0) {
				memcpy(mat.data, buffer.data(), buffer.size());
			} else {
				cv::imdecode(buffer, cv::IMREAD_ANYDEPTH, &mat);
			}

			if (callbackCv) callbackCv(mat);
			if (callbackPcl) {

				const uint16_t *depthMap = reinterpret_cast<const uint16_t *>(mat.data);

				cloud->width = header.width;
				cloud->height = header.height;
				cloud->is_dense = false;
				cloud->points.resize(cloud->height * cloud->width);


				float fx = header.fov / float(OpenNI2FloatConversion); // Horizontal focal length
				float fy = fx; // Vertcal focal length
				float cx = ((float) cloud->width - 1.f) / 2.f;  // Center x
				float cy = ((float) cloud->height - 1.f) / 2.f; // Center y

				float fx_inv = 1.0f / fx;
				float fy_inv = 1.0f / fy;

				int depth_idx = 0;
				float bad_point = std::numeric_limits<float>::quiet_NaN();

				for (int v = 0; v < cloud->height; ++v) {
					for (int u = 0; u < cloud->width; ++u, ++depth_idx) {
						pcl::PointXYZ &pt = cloud->points[depth_idx];
						/// @todo Different values for these cases
						// Check for invalid measurements
						if (depthMap[depth_idx] == 0) {
//							depthMap[depth_idx] == depth_image->getNoSampleValue () ||
//							depthMap[depth_idx] == depth_image->getShadowValue ())
							pt.x = pt.y = pt.z = bad_point;
						} else {
							pt.z = depthMap[depth_idx] * 0.001f; // millimeters to meters
							pt.x = (static_cast<float> (u) - cx) * pt.z * fx_inv;
							pt.y = (static_cast<float> (v) - cy) * pt.z * fy_inv;
						}
					}
				}
				cloud->sensor_origin_.setZero();
				cloud->sensor_orientation_.setIdentity();

				callbackPcl(cloud);
			}

			auto finish = std::chrono::high_resolution_clock::now();

			std::chrono::duration<double> elapsed = finish - start;

			fps = (1.f / elapsed.count()) * 1000;
		}

		acceptor->close();
		acceptor.reset();
		socket->close();
		socket.reset();
		ioService->stop();
		ioService.reset();
	});
}

float OpenNI2NetClient::getFps() {
	return fps / 1000.f;
}


