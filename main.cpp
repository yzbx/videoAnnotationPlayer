#include <QCoreApplication>
#include <QSql>
#include <opencv2/opencv.hpp>
#include <QtCore>
#include <iostream>
#include <stdio.h>
#include <QDebug>
#include <vector>
#include <QVector>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>

using namespace cv;
using namespace std;

typedef struct
{
    int fid;    //frame_number
    int oid;    //object_id
    int x1;
    int x2;
    int y1;
    int y2;
}MyObject;

//db 为全局轨迹数据库变量
QSqlDatabase db;
//videopath 为视频位置
QString videopath;

//初始化全局变量
void init(QString video_database_path, int videoId);

//轨迹数据库的路径，轨迹存储在sqlite文件中，需要知道sqlite文件的路径
void LoadDatabase(QString trajectory_database_path);

//获取当前帧frameId 时所有目标位置从而在播放时标记目标位置
QVector<MyObject> getMyObjects(int frameId);

//视频数据库的路径,视频路径存储在sqlite文件中，需要知道sqlite文件的路径
QString getTrajectoryDBPath(QString video_database_path);

//假设视频与相应的sqlite在同一文件下，文件前缀相同，仅后缀.avi不同于后缀.sqlite
QString getTDP(QString video_path);

//配置文件config.ini 存放在本地目录下
QSettings* getConfig();

//播放视频并显示相应的目标位置标志
void playAnnotationVideo();

int main(int argc, char *argv[])
{
    //QCoreApplication a(argc, argv);
    //return a.exec();

    if(argc<3){
        cout<<"usage: xxx.exe video_database_path videoId"<<endl;
        exit(-1);
    }
    QString video_database_path=QString::fromStdString(argv[1]);

    stringstream ss;
    ss<<argv[2];

    int videoId;
    ss>>videoId;

    cout<<"input "<<video_database_path.toStdString()<<" "<<videoId<<endl;
    init(video_database_path,videoId);

    playAnnotationVideo();
}

void init(QString video_database_path,int videoId){
    QSqlDatabase videoDb = QSqlDatabase::addDatabase("QSQLITE","video");
    videoDb.setDatabaseName(video_database_path);
    if (!videoDb.open()){
        qDebug()<<"open db failed, please check the path "<<video_database_path;
        qDebug()<<videoDb.lastError();
    }

    QString querystr=QString("SELECT dir FROM video where id==%1").arg(videoId);
    QSqlQuery query(querystr,videoDb);

    if(!query.next()){
        qDebug()<<"querystr is "<<querystr;
        qDebug()<<"error video db donot return resutl";
        exit(-1);
    }

    videopath=query.value(0).toString();
    QString trajectoryPath=getTDP(videopath);

    LoadDatabase(trajectoryPath);

    videoDb.close();
}

QString getTDP(QString video_path)
{
    QString trajectoryPath=video_path.replace(".avi",".sqlite");
    return trajectoryPath;
}

void LoadDatabase(QString trajectory_database_path){
    db = QSqlDatabase::addDatabase("QSQLITE","trajectory");
    db.setDatabaseName(trajectory_database_path);
    if (!db.open()){
        qDebug()<<"open db failed, please check the path "<<trajectory_database_path;
        qDebug()<<db.lastError();
    }
}

QVector<MyObject> getMyObjects(int frameId)
{
    QString querystr=QString("SELECT * FROM bounding_boxes where frame_number==%1").arg(frameId);
    QSqlQuery query(querystr,db);

    QVector<MyObject> vec;
    while (query.next()) {
        MyObject obj;
        bool ok;
        obj.fid=query.value(1).toInt(&ok);
        obj.oid=query.value(0).toInt(&ok);
        obj.x1=int(query.value(2).toDouble(&ok));
        obj.y1=int(query.value(3).toDouble(&ok));
        obj.x2=int(query.value(4).toDouble(&ok));
        obj.y2=int(query.value(5).toDouble(&ok));
        vec.push_back(obj);
    }

    if(vec.empty()) qDebug()<<"MyObjects is empty "<<querystr;

    return vec;
}

QSettings* getConfig(){
    return new QSettings("config.ini", QSettings::IniFormat);
//        show = setting->value("/fullPic/show").toBool();
//        save = setting->value("/fullPic/save").toBool();
}

void playAnnotationVideo(){
    int VideoPos=0;
    VideoCapture capture(videopath.toStdString());

    QSettings* setting=getConfig();
    bool ok;
    bool foregroundShow=setting->value("/foreground/show").toBool();
    bool backgroundShow=setting->value("/background/show").toBool();
    bool videoPlaySave=setting->value("/videoPlay/save").toBool();
    int frameNumSave=setting->value("/videoPlay/frameNum").toInt(&ok);


    int frameTotalNum=capture.get(CV_CAP_PROP_FRAME_COUNT);

    QString querystr="select min(frame_number), max(frame_number) from bounding_boxes";
    QSqlQuery query(querystr,db);

    int max,min;
    if(query.next()){
        bool ok;
        min=query.value(0).toInt(&ok);
        max=query.value(1).toInt(&ok);
    }
    else{
        qDebug()<<"cannot get max and min frame_number";
        exit(-1);
    }


    qDebug()<<"max="<<max<<" min="<<min<<" frameTotalNum="<<frameTotalNum;
    if(max>frameTotalNum)   max=frameTotalNum;

    Mat frame,frameCopy,foreground,background;
    QString foregroundDir=videopath;
    QString backgroundDir=videopath;
    QString videoPlayDir=videopath;
    videoPlayDir.replace(".avi","-videoPlay");
    foregroundDir.replace(".avi","-foreground");
    backgroundDir.replace(".avi","-background");

    QDir dir;
    dir.mkdir(videoPlayDir);
    while(VideoPos<min){
        if(VideoPos==0){
            capture>>frame;
            frameCopy=frame.clone();
        }
        else{
            Mat frameClone;
            capture>>frameClone;
            Scalar s1=sum(frameClone-frameCopy);
            Scalar s2=sum(frameCopy-frameClone);
            cout<<"s1 "<<s1<<" s2 "<<s2<<endl;
            while(s1(0)==0&&s1(1)==0&&s1(2)==0&&s2(0)==0&&s2(1)==0&&s2(2)==0){
                cout<<"repeat: s1 "<<s1<<" s2 "<<s2<<endl;
                capture>>frameClone;
                s1=sum(frameClone-frameCopy);
                s2=sum(frameCopy-frameClone);
            }
            frameClone.copyTo(frame);
            frameClone.copyTo(frameCopy);
        }



        VideoPos++;
        if(videoPlaySave&&VideoPos<=frameNumSave){
            char cstr[10];
            sprintf(cstr,"%08d",VideoPos);

            QString videoPlayPath=QString("%1\\%2.png").arg(videoPlayDir).arg(QString(cstr));

            imwrite(videoPlayPath.toStdString(),frame);
        }
    }

    stringstream ss;
    string text;
    //frame 0,1,2...
    //VideoPos 1,2,3...
    //foreground 00000001.png,00000002.png,00000003.png
    namedWindow("videoAnnotationPlayer",0);
    if(foregroundShow)  namedWindow("foreground",0);
    if(backgroundShow)  namedWindow("background",0);


    while(VideoPos<=max){
        if(VideoPos==0){
            capture>>frame;
            frameCopy=frame.clone();
        }
        else{
            Mat frameClone;
            capture>>frameClone;
            Scalar s1=sum(frameClone-frameCopy);
            Scalar s2=sum(frameCopy-frameClone);
            cout<<"s1 "<<s1<<" s2 "<<s2<<endl;
            while(s1(0)==0&&s1(1)==0&&s1(2)==0&&s2(0)==0&&s2(1)==0&&s2(2)==0){
                cout<<"repeat: s1 "<<s1<<" s2 "<<s2<<endl;
                capture>>frameClone;
                s1=sum(frameClone-frameCopy);
                s2=sum(frameCopy-frameClone);
            }
            frameClone.copyTo(frame);
            frameClone.copyTo(frameCopy);
        }

        QVector<MyObject> vec=getMyObjects(VideoPos);
        VideoPos=VideoPos+1;
        if(foregroundShow){
            char cstr[10];
            sprintf(cstr,"%08d",VideoPos);

            QString foregroundPath=QString("%1\\%2.png").arg(foregroundDir).arg(QString(cstr));


            foreground=imread(foregroundPath.toStdString());
            if(foreground.empty()){
                qDebug()<<"warnning: foregroundPath is "<<foregroundPath;
            }
        }

        if(backgroundShow){
            char cstr[10];
            sprintf(cstr,"%08d",VideoPos);

            QString backgroundPath=QString("%1\\%2.png").arg(backgroundDir).arg(QString(cstr));


            background=imread(backgroundPath.toStdString());
            if(background.empty()){
                qDebug()<<"warnning: backgroundPath is "<<backgroundPath;
            }
        }

        int m=vec.length();

//        qDebug()<<"m is "<<m;
        for(int i=0;i<m;i++){
            MyObject ob=vec.value(i);
            Rect rect(Point(ob.x1,ob.y1),Point(ob.x2,ob.y2));
            int r=ob.oid%27;
            int b=r%3;
            r=r/3;
            int g=r%3;
            r=r/3;

            ss<<ob.oid;
            ss>>text;
            ss.clear();

//            cout<<"ob.oid= "<<ob.oid<<" text= "<<text<<endl;
            rectangle(frame,rect,Scalar(50*b,50*g,50*r),5);
            putText(frame,text,Point(ob.x1,ob.y1),CV_FONT_HERSHEY_COMPLEX,1,Scalar(0,0,255),5);

            if(foregroundShow){
                rectangle(foreground,rect,Scalar(50*b,50*g,50*r),5);
                putText(foreground,text,Point(ob.x1,ob.y1),CV_FONT_HERSHEY_COMPLEX,1,Scalar(0,0,255),5);
            }
            if(backgroundShow){
                rectangle(background,rect,Scalar(50*b,50*g,50*r),5);
                putText(background,text,Point(ob.x1,ob.y1),CV_FONT_HERSHEY_COMPLEX,1,Scalar(0,0,255),5);
            }
        }

        ss<<VideoPos<<"/"<<max;
        ss>>text;
        ss.clear();

        putText(frame,text,Point(30,30),CV_FONT_HERSHEY_COMPLEX,1,Scalar(0,0,255),5);
        imshow("videoAnnotationPlayer",frame);
        imshow("foreground",foreground);
        imshow("background",background);
        if(videoPlaySave&&VideoPos<=frameNumSave){
            char cstr[10];
            sprintf(cstr,"%08d",VideoPos);
            QString videoPlayPath=QString("%1\\%2.png").arg(videoPlayDir).arg(QString(cstr));
            imwrite(videoPlayPath.toStdString(),frame);
        }
        waitKey(30);

        qDebug()<<VideoPos<<"/"<<max;
    }

}
