#include "Poco/Util/XMLConfiguration.h"
#include "Poco/RegularExpression.h"
#include "Poco/DirectoryIterator.h"
#include "Poco/StreamCopier.h"
#include "Poco/Thread.h"
#include "Poco/File.h"
#include "Poco/Exception.h"
#include "Poco/Glob.h"


#include <Poco/Logger.h>
#include <Poco/PatternFormatter.h>
#include <Poco/FormattingChannel.h>
#include <Poco/ConsoleChannel.h>
#include <Poco/Util/Timer.h>
#include <Poco/Util/TimerTask.h>
#include <Poco/Util/TimerTaskAdapter.h>
#include <Poco/Event.h>
#include <Poco/Timestamp.h>
#include <Poco/DateTime.h>
#include <Poco/Timespan.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Format.h>


#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>



//取结构的一部分
struct Diamond4Head
{
	double lon_span, lat_span;
	double min_lon, max_lon, min_lat, max_lat;
	int lon_num, lat_num;
	int overlay;

	Diamond4Head()
	{
		lon_span = lat_span = 0;
		min_lon = max_lon = min_lat = max_lat = lon_num = lat_num = 0;
		overlay = 0;
	}

	bool operator == (const Diamond4Head& right)const
	{
		if(    min_lon == right.min_lon
			&& max_lon == right.max_lon
			&& min_lat == right.min_lat
			&& max_lat == right.max_lat
			&& lon_num == right.lon_num
			&& lat_num == right.lat_num
			&& overlay == right.overlay) return true;
		else return false;
	}
	bool operator != (const Diamond4Head& right)const
	{
		if(right == *this) return false;
		else return true;
	}
};

bool read_diamond4(const std::string &path, Diamond4Head & head, std::vector<double> &vec)
{
	std::ifstream fin(path);
	if( !fin.is_open() ) return false;

	std::string temp;
	
	fin>>temp>>temp>>temp>>temp
		>>temp>>temp>>temp>>temp;

	fin>>head.overlay>>head.lon_span>>head.lat_span;

	fin>>head.min_lon>>head.max_lon>>head.min_lat>>head.max_lat;
	fin>>head.lon_num>>head.lat_num;

	fin>>temp>>temp>>temp>>temp>>temp;

	double val;
	vec.clear();

	while(fin>>val)
	{
		vec.push_back(val);
	}

	if(vec.size() != head.lon_num * head.lat_num ) return false;

	return true;
}

double op_accretion_index (double T, double rh)
{ 
	if(T>0 || T<-14 || rh<50) return 0;
	else return 2 * ( rh-50 ) * ( T * (T+14)/-49 ); 
}

int op_accretion_VV (double I, double w)
{
	int result = 0;

	if(w <= -0.2 )
	{
		if(I >=0 && I <40) result = 1;
		else if(I >=40 && I <70) result = 2;
		else if(I >= 70) result = 3;
	}
	return result; 
}


//利用某一天某一时次T639资料计算积冰
//------------------------------------------------------
class T639IceAccretion{

public:
	T639IceAccretion(std::string micapsDirInput = "W:", int dayDiff = 0, int hour = 8, std::string outDirInput = "F:/test/639" )
	{
		micapsDir = micapsDirInput;
		tDir = micapsDir + "/T_4";
		rhDir = micapsDir + "/RH_4";
		omegaDir = micapsDir + "/OMEGA_4";

		outDir = outDirInput;

		beginDay = dayDiff;
		beginHour = hour;

	}

	//每一层 ——> 每一个时次 ——> 温度、湿度、垂直速度
	void calc_all_layers()
	{
		Poco::LocalDateTime current;
		
		current -= Poco::Timespan(0, current.hour(), 0, 0, 0);
		begintime = current + Poco::Timespan(beginDay, beginHour, 0, 0, 0);

		std::vector<std::string>  layers;
		get_layers(layers);

		std::vector<std::string>::const_iterator it = layers.begin();

		for(; it != layers.end(); it++)
		{
			calc_layer( *it );
		}
	}

	void onTimer(Poco::Util::TimerTask& task)
	{
		calc_all_layers();
	}

	void generate_head(const std::string& type, std::string& headstr)
	{
		int period;

		std::istringstream ssin(curPeriod);
		ssin>>period;

		Poco::LocalDateTime forcastTime = begintime + Poco::Timespan(period*Poco::Timespan::HOURS);
		
		std::string beginstr = Poco::DateTimeFormatter::format(begintime, "%y%m%d%H"),
			beginstr2 = Poco::DateTimeFormatter::format(begintime, "%y %m %d %H"),
			forcastStr = Poco::DateTimeFormatter::format(forcastTime, "%m月%d日%H时");
		headstr = "diamond 4 "+beginstr+"_"+forcastStr+"T639_";

		std::ostringstream sout1, sout2;

		sout1<<curHead.overlay<<"hPa";
		sout2<<beginstr2<<' '<<period<<' '<<curHead.overlay<<' '<<curHead.lon_span<<' '<<curHead.lat_span;
		sout2<<' '<<curHead.min_lon<<' '<<curHead.max_lon<<' '<<curHead.min_lat<<' '<<curHead.max_lat;
		sout2<<' '<<curHead.lon_num<<' '<<curHead.lat_num;
		if(type == "II")
		{
			headstr += sout1.str() + "积冰指数预报 " + sout2.str() + " 10 0.00 100.00 2 50.00";
		}
		else if(type == "VV")
		{
			headstr += sout1.str() + "积冰程度预报 " + sout2.str() + " 1 0.00 3.00 2 1.00";
		}
	}

	void generate_file(const std::vector<double>& vec, const std::string& type, const std::string& layer, const std::string &filename)
	{
		Poco::File(outDir+'/'+type+'/'+layer).createDirectories();
		std::ofstream fout(outDir+'/' + type+'/'+layer+ '/'+ filename);

		std::string headstr;
		generate_head(type, headstr);

		fout<<headstr;

		std::vector<double>::size_type it = 0;

		for(; it != vec.size(); it++)
		{
			if(! (it%curHead.lon_num) ) fout<<std::endl;
			fout<<vec[it]<<' ';
		}
		fout.close();
	}

	void calc_file(const std::string& layer, const std::string& filename)
	{
		Diamond4Head headT, headRh, headW;
		std::vector<double> vecT, vecRh, vecI, vecW, vecV;
		//"W:/T639/T_4/600/13012002.000"
		bool validT = read_diamond4(tDir+'/'+layer+'/'+ filename, headT, vecT),
			validRh = read_diamond4(rhDir+'/'+layer+'/'+ filename, headRh, vecRh),
			validW = read_diamond4(omegaDir+'/'+layer+'/'+ filename , headW, vecW);

		if( validT && validRh && headT == headRh )
		{//可以计算指数
			curHead = headT;

			vecI.resize(vecT.size() );
			std::transform(vecT.begin(), vecT.end(), vecRh.begin(), vecI.begin(), op_accretion_index );

			generate_file(vecI,"II", layer, filename );

			if( validW && headT == headW )
			{//计算预报
				vecV.resize(vecT.size() );
				std::transform(vecI.begin(), vecI.end(), vecW.begin(), vecV.begin(), op_accretion_VV );

				generate_file(vecV,"VV", layer, filename );
			}
		}
		else
		{
			std::cout<<" read T or Rh false ";
		}
	}

	void calc_layer(const std::string& layer)
	{
		
		std::string daystr = Poco::DateTimeFormatter::format(begintime, "%y%m%d%H");//
		
		std::set<std::string> periods;

		std::cout<<"\n\n 正在获取"<<layer<<" hPa预报时效 ";
		get_period(layer, daystr, periods);

		if(periods.size() == 0 )
		{
			std::cout<<"\n 没有当天数据！";
		}
		else
		{
			std::cout<<"\n正在计算"<<layer<<" hPa数据...   ";
		}

		std::set<std::string>::iterator it = periods.begin();

		for (; it != periods.end(); ++it)
		{
			curPeriod = *it;
			calc_file(layer, daystr + '.' + *it);
		}
		std::cout<<"计算完成";
	}

private:
	std::string micapsDir, tDir, rhDir, omegaDir, outDir;
	int beginDay, beginHour;

	Diamond4Head curHead;
	Poco::LocalDateTime begintime;

	std::string curPeriod;

private:
	void get_layers(std::vector<std::string> & layers)
	{
		
		Poco::DirectoryIterator pfile( tDir ), end;

		layers.clear();

		std::cout<<"\n正在获取预报层次：";
		while (pfile != end)
		{
			if( pfile->isDirectory() )
			{
				layers.push_back( pfile.name() );

				std::cout << pfile.name() << std::ends;

			}

			pfile++;
		}
	}

	void get_period(const std::string& layer, const std::string& daystr, std::set<std::string> & periods)
	{
		std::set<std::string> files;
		Poco::Glob::glob(tDir+'/'+layer+'/'+daystr+".*", files);

		std::cout<<tDir+'/'+layer+'/'+daystr+".*\n";

		std::set<std::string>::iterator it = files.begin();

		periods.clear();

		for (; it != files.end(); ++it)
		{
			Poco::Path path(*it);

			periods.insert( path.getExtension() );

			std::cout << path.getExtension() << std::ends;
		}
	}
};
//------------------------------------------------------


//获取配置数据
//------------------------------------------------------
std::string getDataDir()
{
	using Poco::AutoPtr;
	using Poco::Util::XMLConfiguration;

	AutoPtr<XMLConfiguration> pConf(new XMLConfiguration("jibing-config.xml") );
	
	return pConf->getString("micapsdir", "W:");
}


std::string getOutDir()
{
	using Poco::AutoPtr;
	using Poco::Util::XMLConfiguration;

	AutoPtr<XMLConfiguration> pConf(new XMLConfiguration("jibing-config.xml") );
	
	return pConf->getString("outdir", "F:/test/639");
}

int getT639Deal08()
{
	using Poco::AutoPtr;
	using Poco::Util::XMLConfiguration;

	AutoPtr<XMLConfiguration> pConf(new XMLConfiguration("jibing-config.xml") );
	
	return pConf->getInt( "T639Deal08", 16);
}

int getT639Deal20()
{
	using Poco::AutoPtr;
	using Poco::Util::XMLConfiguration;

	AutoPtr<XMLConfiguration> pConf(new XMLConfiguration("jibing-config.xml") );
	
	return pConf->getInt( "T639Deal20", 6);
}
//------------------------------------------------------

void printAppInfo()
{
	std::cout<<"积冰计算程序 V1.2 \n"
			   "by xufanglu(20130525)\n"
			   "-----------------------------------------\n\n";

}


int main()
{
	printAppInfo();
	/*
	*/
	std::string micapsDir = getDataDir(), outDir = getOutDir();

	std::cout<<"Micaps T639数据目录已设置为 "+micapsDir+" 。"<<std::endl;
	std::cout<<"程序输出目录已设置为 "+outDir+" 。"<<std::endl;

	T639IceAccretion t639_08(micapsDir, 0, 8, outDir), t639_20(micapsDir, -1, 20, outDir);
	

	Poco::Util::Timer timerT639;

	Poco::Util::TimerTask::Ptr pTask08 = new Poco::Util::TimerTaskAdapter<T639IceAccretion>(t639_08, &T639IceAccretion::onTimer),
		pTask20 = new Poco::Util::TimerTaskAdapter<T639IceAccretion>(t639_20, &T639IceAccretion::onTimer);


	Poco::LocalDateTime current, today00, deal08T639Time, deal20T639Time;
		
	today00 = current - Poco::Timespan(0, current.hour(), current.minute(), current.second(), 0);

	int deal08hour = getT639Deal08(), deal20hour = getT639Deal20();

	std::cout<<"08时T639资料处理时间已设置为 "<<deal08hour<<"时。"<<std::endl;
	std::cout<<"20时T639资料处理时间已设置为 "<<deal20hour<<"时。"<<std::endl;

	deal20T639Time = today00 + Poco::Timespan(0, deal20hour, 0, 0, 0);
	deal08T639Time = today00 + Poco::Timespan(0, deal08hour, 0, 0, 0);

	timerT639.scheduleAtFixedRate(pTask20, (deal20T639Time - current).totalMilliseconds(), 1000*3600*24 );
	timerT639.scheduleAtFixedRate(pTask08, (deal08T639Time - current).totalMilliseconds(), 1000*3600*24 );

	/*
	std::cout<<"当前时间  "<<Poco::DateTimeFormatter::format(current, "%Y-%m-%d %H:%M:%S.%i.%F")<<std::endl;
	std::cout<<"08时T639资料处理时间  "<<Poco::DateTimeFormatter::format(deal08T639Time, "%Y-%m-%d %H:%M:%S.%i.%F")<<std::endl;
	std::cout<<"20时T639资料处理时间  "<<Poco::DateTimeFormatter::format(deal20T639Time, "%Y-%m-%d %H:%M:%S.%i.%F")<<std::endl;
	std::cout<<"(deal08T639Time - current).totalMilliseconds()  "<<(deal08T639Time - current).totalMilliseconds()<<std::endl;
	*/
	
	while(true){}
	return 0;
}