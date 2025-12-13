#include "Handler/DataHandler.h"
#include "server.h"
#include <filesystem>
#include <fstream>

DataSyncHandler::DataSyncHandler(Server* server): HttpHandler(server) {

}

void DataSyncHandler::get(const httplib::Request& req, httplib::Response& res) {
    auto& cfg = _server->GetConfig();
    auto datapath = cfg.GetDatabasePath();
    auto bakup_path = datapath + "/zh.backup";
    nlohmann::json data;
    if (std::filesystem::exists(bakup_path) && !_server->IsDataLock()) {
        // 压缩
        auto zip_path = datapath + "/bak.zip";
        String excp;
        if (!CreateZip(bakup_path, zip_path, excp)) {
            res.status = 401;
            res.set_content(String("{error: '") + excp + "'}", "application/json");
            return;
        }
        SendFile(zip_path, res);
        res.status = 200;
    }
    else {
        res.status = 200;
    }
}

void DataSyncHandler::SendFile(const String& filepath, httplib::Response& res) {
    std::ifstream* file = new std::ifstream(filepath, std::ios::binary);
    res.set_chunked_content_provider(
        "application/octet-stream", // Content-Type
        [file, filepath](size_t offset, httplib::DataSink &sink) {
            char buffer[MAX_STREAM_SIZE] = {0};
            file->read(buffer, sizeof(buffer));
            std::streamsize bytes_read = file->gcount();
            if (bytes_read > 0) {
                sink.write(buffer, bytes_read);
                return true;
            }
            else {
                sink.done();
                file->close();
                delete file;
                //std::filesystem::remove_all(filepath);
                return false;
            }
        }
    );
}

bool DataSyncHandler::CreateZip(const String& dirs, const String& dstfile, String& response) {
    std::filesystem::path path(dirs), dstpath(dstfile);
    auto dir_name = path.filename().string();
    auto dstname = dstpath.filename().string();
#ifdef __linux__
    String conn = "&&";
    String compress = "7z a -tzip -mmt=on -mfb=1024 " + dstname + " " + dir_name;
#else
    String conn = "&";
    String compress = "zip -qr " + dstname + " " + dir_name;
#endif
    String cmd = "cd " + path.parent_path().string() +" " + conn +" " + compress;
    LOG("{}", cmd);
    _server->LockData();
    bool res = RunCommand(cmd, response);
    _server->FreeDataLock();
    return res;
}
