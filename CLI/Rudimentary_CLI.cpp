#include<bits/stdc++.h>
#include<unistd.h>
#include<sys/wait.h>
#include<fcntl.h>

using namespace std;

//prints the current working directory 
void printDir() 
{ 
    char cwd[1024]; 
    getcwd(cwd, sizeof(cwd)); 
    printf("\nCurrent Directory: %s\n", cwd); 
}

// trim from beginning(in place)
string begtrim(string s) {
    s.erase(s.begin(),find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !isspace(ch);
    }));
 	return s;
}

// trim from end (in place)
string endtrim(string s) {
    s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !isspace(ch);
    }).base(), s.end());
    return s;
}

//splits the command into individual arguments
vector<string> getcmdargs(string s)
{
	vector<string> ans;
	stringstream ss(s);
	string temp;
	while(ss>>temp)
	{
		ans.push_back(temp);
	}
	return ans;
}


//splits the input according to the delimiter 'del'
vector<string> split_up_cmd(string s, char del)
{
    vector<string> ans;
    stringstream ss(s); 
    string temp;

    // Read from the string stream till the delimiter
    while(getline(ss, temp, del))
    {
        ans.push_back(temp);
    }
    
    return ans;
}

// Open files and redirect input and output with files as arguments
void redirect(string in_file, string out_file)
{
    int fd_in, fd_out;

    // Open input redirecting file
    if(!in_file.empty())
    {
        //Open in the read only mode
        fd_in = open(in_file.c_str(),O_RDONLY);  
        if(fd_in < 0)
        {
            cout<<"\033[1;31mFile opening failed!!: "<< in_file <<"\033[0m\n"<<endl;
            exit(EXIT_FAILURE);
        }
        // Redirect the input using dup2
        if( dup2(fd_in,STDIN_FILENO) < 0 )
        {
            cout<<"\033[1;31mERROR!! Input Redirection failed\033[0m\n"<<endl;
            exit(EXIT_FAILURE);
        }
    }

    // Open output redirecting file
    if(!out_file.empty())
    {
        //Open file in create, write only mode 
        fd_out = open(out_file.c_str(), O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);  
        // Redirect output using dup2
        if( dup2(fd_out,STDOUT_FILENO) < 0 )
        {
            cout<<"\033[1;31mERROR!! Output Redirection failed\033[0m\n"<<endl;
            exit(EXIT_FAILURE);
        }
    }
}

//Execute the command that are not piped
void runcmd(vector<string>parts,bool flag_bg,int status)
{
	string s=parts[0];
    vector<string> args=getcmdargs(s);
    char* argv[args.size()+1];
    for(int i=0 ; i<args.size() ; i++)
    {
        argv[i] = const_cast<char*>(args[i].c_str()); // Convert string to char *
    }
    argv[args.size()] = NULL; //Arguments array should be NULL terminated
    char* const* argv1 = argv; // Assign it to a constant array
		    if(strcmp(argv1[0],"cd")==0)
		    {
		        if(chdir(argv1[1])!=0)
		        {
		        	cout<<"\033[1;31mError!! No such directory or file exist\033[0m\n"<<endl;
		        }
				printDir();
		    }
		    else
		    {
		    	pid_t pid = fork();
		    	if (pid == 0)
		        {
		    	 redirect(parts[1],parts[2]);
    			 execvp(argv1[0],argv1); 
    			 exit(0);
    		   }
    		   if(!flag_bg)
	                while(wait(&status)>0);
               //usleep(10000);
               // else
               //      flag_bg = false;
    		}
}

//Function to run each individual command from the piped command
void runcmdfp(string s)
{

    vector<string> args=getcmdargs(s);
    char* argv[args.size()+1];
    for(int i=0 ; i<args.size() ; i++)
    {
        argv[i] = const_cast<char*>(args[i].c_str()); // Convert string to char *
    }
    argv[args.size()] = NULL; //Arguments array should be NULL terminated
    char* const* argv1 = argv; 
    execvp(argv1[0],argv1); 
}


//Seperate input and output from the input string
vector<string> sep_input_output(string cmd)
{
    vector<string> res(3);
  	string ans,in,out;
  	bool l=0,g=0;

    //l acts as flag for '<' and g is a flag for '>'
    for(int i=0;i<cmd.size();i++)
    {
        //unique state {l=1,g=0},when < symbol is encountered
    	if(cmd[i]=='<')
    	{
    		l=1;
    		g=0;
    	}
        //unique state {l=0,g=1} when > symbol is encountered
    	else if(cmd[i]=='>')
    	{
    		g=1;
    		l=0;
    	}
    	else
    	{
            //consider the string as command till any of the symbol is encountered
	    	if(l==0 && g==0)
	    	{
	    		ans+=cmd[i];
	    	}
            /*consider the string as input file name when '<' is encountered till any other symbol is encountered*/
	    	else if(l==1)
	    	{
	    		in+=cmd[i];
	    	}
            /*consider the string as output file name when '>' is encountered till any other symbol is encountered*/
	    	else if(g==1)
	    	{
	    		out+=cmd[i];
	    	}
    	}
    }
    //remove white spaces if any
    if(ans.size()>0)
    {
    	res[0]=endtrim(begtrim(ans));
    }
    if(in.size()>0)
    {
    	res[1]=endtrim(begtrim(in));
    }
    if(out.size()>0)
    {
    	res[2]=endtrim(begtrim(out));
    }
    return res;
    
} 


//Execute piped commands
void execPipedCmds(vector<string> cmds,bool flag_bg,int status)
{
	int n=cmds.size(); 
    int currFD[2], prevFD[2];
    for(int i=0; i<n; i++)
     {
                vector<string> parts = sep_input_output(cmds[i]);
                if(i!=n-1) 
                {
                	if(pipe(currFD)<0)
                	cout<<"\033[1;31mError in creating pipe!!\033[0m\n"<<endl;
                }
                pid_t pid=fork();
                if(pid==0)
                {
                	if(i==0 || i==n-1)
                	{
                		redirect(parts[1],parts[2]);
                	}
                	if(i!=0)
                	{
                		dup2(prevFD[0],STDIN_FILENO);
                		close(prevFD[0]);
                		close(prevFD[1]);
                	}
                	if(i!=n-1)
                	{
                		dup2(currFD[1],STDOUT_FILENO);
                		close(currFD[0]);
                		close(currFD[1]);
                	}

                	runcmdfp(parts[0]);
                }
                if(i!=0)
                {
                    close(prevFD[0]), close(prevFD[1]);
                }
                if(i!=n-1)
                {
                	prevFD[0]=currFD[0];
                	prevFD[1]=currFD[1];               	
                }

     }

            // If no background, then wait for all child processes to return
            if(!flag_bg)
                while(wait(&status) > 0);

 }

int main()
{
    
    string input_cmd;
    int status = 0;
    cout << "\033[1;33m> Enter q to exit from the shell!!\033[0m\n";
    bool flag_bg = false;
    while(true)
    {
        // flag check for background running 

        // Get input 
        if(flag_bg==true)
        {
            flag_bg=false;
        }
        cout<<"\033[1;35mEnter Command:~$ \033[0m";
        getline(cin, input_cmd); 
        //remove white spaces at the beginning and the end of the input      
        input_cmd = endtrim(begtrim(input_cmd));
        if(input_cmd=="q")
        {
        	exit(0);
        }
        else
        {
        	 // Check for background run
	        if( input_cmd.back() == '&')
	        {
	            flag_bg = true;
	            input_cmd.back() = ' ';
	        }


	        // Split into several commands according to |
	        vector<string> cmds = split_up_cmd(input_cmd, '|');

	        // If no pipes are required
	        if(cmds.size()==1)
	        {
	            
	            vector< string > parts = sep_input_output(cmds[0]);
		        runcmd(parts,flag_bg,status); // Execute the command
	        }
            // For piped commands
	        else
	        {
	        	execPipedCmds(cmds,flag_bg,status);
	        }
            
    	}
    }
}