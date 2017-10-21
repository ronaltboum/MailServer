#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "messages.h" 
#include "mail.h"

char userName [MAX_USERNAME+1];

int check_user(Node *node_to_check);
int send_mail_to_client(int client_socket, Node *mailNode);
int handle_show_inbox(int client_socket);
int handle_compose(int client_socket, const char* client_input);
int handle_delete(int client_socket, const char* client_parameters);
int handle_getMail(int client_socket, const char* client_parameters); 
void saveUsername(const char* line);
int check_username_passowrd_exist(const char* data, FILE* fp);
int command_login(int client_sock, const char* data, FILE* fp);
int server_to_client(int client_sock, char usersFilePath[]);
int initiate_server(unsigned short int port, char usersFilePath[]);



//returns 0 if the mail in node_to_check is in the inbox of the current client,  and -1 otherwise
int check_user(Node *node_to_check){
	if( strcmp(node_to_check->email.username, userName) == 0 )
		return 0;
	return -1;
}


int send_mail_to_client(int client_socket, Node *mailNode){
	//send the mail_id
	protocol_message msg;
	msg.header.opcode = OPCODE_MAIL_ID;
	memset(msg.data, '\0', MAX_DATA_SIZE);
	sprintf(msg.data,"%d", mailNode->email.mail_id );
	msg.header.data_length = strlen(msg.data);
	
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;
	//send the mail sender
	protocol_message sender_msg;
	sender_msg.header.opcode = OPCODE_SENDER;
	sender_msg.header.data_length = strlen(mailNode->email.mail_sender);
	memset(sender_msg.data, '\0', MAX_DATA_SIZE);
	strncpy(sender_msg.data, mailNode->email.mail_sender, strlen(mailNode->email.mail_sender));
	if (send_protocol_message(client_socket, &sender_msg)) 
		return STATE_ERROR;
	//send the subject
	protocol_message subject_msg;
	subject_msg.header.opcode = OPCODE_SUBJECT;
	subject_msg.header.data_length = strlen(mailNode->email.subject);
	memset(subject_msg.data, '\0', MAX_DATA_SIZE);
	strncpy(subject_msg.data, mailNode->email.subject, strlen(mailNode->email.subject));
	if (send_protocol_message(client_socket, &subject_msg)) 
		return STATE_ERROR;

	return STATE_SUCCESS;	
}

int handle_show_inbox(int client_socket){
	//OPCODE_SHOW_INBOX_END		OPCODE_MAIL_ID		OPCODE_SENDER		OPCODE_SUBJECT
	//find the first Node in the linked list of mails(if exists) with the client's username:
	mail mailToFind;	Node* mailNode;	  
	memset(mailToFind.username, '\0', sizeof(mailToFind.username));
	strcpy(mailToFind.username, userName);
	mailNode = findUser(mailToFind);
	int state_flag = 0;
	if(mailNode != NULL)
	{
		//send the first mail to the client
		if(mailNode->email.is_deleted != -88) 
		{
			state_flag = send_mail_to_client(client_socket, mailNode);
			if (state_flag != STATE_SUCCESS) {
				return state_flag;
			}
		}
	}
	else {
		protocol_message msg;
		msg.header.opcode = OPCODE_SHOW_INBOX_END;
		msg.header.data_length = 0;
		memset(msg.data, '\0', MAX_DATA_SIZE);
		if (send_protocol_message(client_socket, &msg)) 
			return STATE_ERROR;
		return STATE_SUCCESS;	
	}

	//iterate over the linked list of mails until reaching a different username
	int user = 0;
	mailNode = mailNode->next;   
	while(mailNode != NULL){
		user = check_user(mailNode);
		if(user == -1)
			break;

		//make sure mail isn't marked deleted
		if(mailNode->email.is_deleted == -88){
			mailNode = mailNode->next;
			continue;
		}

		state_flag = send_mail_to_client(client_socket, mailNode);
		if (state_flag != STATE_SUCCESS) {
			return state_flag;
		}
		mailNode = mailNode->next;
	}

	//send end message to client
	protocol_message msg;
	msg.header.opcode = OPCODE_SHOW_INBOX_END;
	msg.header.data_length = 0;
	memset(msg.data, '\0', MAX_DATA_SIZE);
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;

	return STATE_SUCCESS;	 
}


int handle_compose(int client_socket, const char* client_input)
{
	mail mail_to_insert;
	
	memset(mail_to_insert.mail_sender, '\0', MAX_USERNAME_SIZE + 1 );
	strncpy(mail_to_insert.mail_sender, userName, MAX_USERNAME);
	memset(mail_to_insert.mail_recievers, '\0', MAX_RECIEVERS * MAX_USERNAME_SIZE +19 +1 );
	strncpy(mail_to_insert.mail_recievers, client_input, strlen(client_input) -1 ); //last char in client_input is \n

	char str[MAX_RECIEVERS * MAX_USERNAME_SIZE +19 +1] = {0};
	strncpy(str, mail_to_insert.mail_recievers, MAX_RECIEVERS * MAX_USERNAME +19 );  //19 for the ","  ,  1 for \0.  no spaces allowed
    	const char s[2] = ",";
    	char *token;
	//recieve SUBJECT message from client:
	protocol_message subjectMsg;
	memset(subjectMsg.data, '\0', MAX_DATA_SIZE);
	int state_flag = recv_protocol_message(client_socket, &subjectMsg);
	if (state_flag != STATE_SUCCESS){
		return state_flag;
	}
	memset(mail_to_insert.subject, '\0', MAX_SUBJECT_SIZE + 1 );
	strncpy(mail_to_insert.subject, subjectMsg.data, strlen(subjectMsg.data) -1 );

	//recieve TEXT message from client:
	protocol_message textMsg;
	memset(textMsg.data, '\0', MAX_DATA_SIZE);
	state_flag = recv_protocol_message(client_socket, &textMsg);
	if (state_flag != STATE_SUCCESS){
		return state_flag;
	}
	memset(mail_to_insert.text, '\0', MAX_TEXT_SIZE + 1 );
	strncpy(mail_to_insert.text, textMsg.data, strlen(textMsg.data) -1 );

	protocol_message server_answer;
	memset(server_answer.data, '\0', MAX_DATA_SIZE);
	int error_occured = 0;
	token = strtok(str, s);
	while( token != NULL ){
	   	memset(mail_to_insert.username, '\0', MAX_USERNAME+1);
		strncpy(mail_to_insert.username, token , MAX_USERNAME);

		//add the mail to the mail list kept in the server:
		int is_error = addMail(mail_to_insert);
		if(is_error == -1){  //server already contains 32,000 mails (including deleted mails)  and cannot add another mail
			error_occured = -1;
		}
		
		token = strtok(NULL, s);
   	}//end of while loop

	if(error_occured == -1) {
		server_answer.header.opcode = OPCODE_COMPOSE_FAILED;
		strcpy(server_answer.data, "Server contains 32,000 mails and is full. Therefore COMPOSE failed");
		server_answer.header.data_length = strlen("Server contains 32,000 mails and is full. Therefore COMPOSE failed");
	}
	else {
		server_answer.header.opcode = OPCODE_COMPOSE_SUCCEED;
		strcpy(server_answer.data, "Mail sent");
		server_answer.header.data_length = strlen("Mail sent");
	}	

	if (send_protocol_message(client_socket, &server_answer)){
		return STATE_ERROR;
	 }
	return STATE_SUCCESS;
}


int handle_delete(int client_socket, const char* client_parameters){

	protocol_message msg;    short mail_id;
	memset(msg.data, '\0', MAX_DATA_SIZE);
	mail_id = atoi(client_parameters);

	mail mailToFind;
	memset(mailToFind.username, '\0', MAX_USERNAME_SIZE+1);
	strcpy(mailToFind.username, userName);
	mailToFind.mail_id = mail_id;
	int result = delete_mail(mailToFind);
	//case where mail wasn't in the list:
	if(result == -1) 
	{
		msg.header.data_length = 0;
		msg.header.opcode = OPCODE_MAIL_NOT_FOUND;
		if (send_protocol_message(client_socket, &msg))
			return STATE_ERROR;
		return STATE_SUCCESS;
	}
	//OPCODE_MAIL_DELETED
	msg.header.data_length = 0;
	msg.header.opcode = OPCODE_MAIL_DELETED;
	if (send_protocol_message(client_socket, &msg))
		return STATE_ERROR;
	return STATE_SUCCESS;
}



int handle_getMail(int client_socket, const char* client_parameters){
	protocol_message msg;    short mail_id;
	memset(msg.data, '\0', MAX_DATA_SIZE);
	mail_id = atoi(client_parameters);

	//assumed the client's username is stored in the global char[] userName
	mail mailToFind;
	memset(mailToFind.username, '\0', MAX_USERNAME_SIZE+1);
	strcpy(mailToFind.username, userName);
	mailToFind.mail_id = mail_id;
	Node *mailNode = GetMail(mailToFind);
	//case where mail wasn't found
	if(mailNode == NULL){

		msg.header.data_length = 0;
		msg.header.opcode = OPCODE_MAIL_NOT_FOUND;
		if (send_protocol_message(client_socket, &msg))
			return STATE_ERROR;
		return STATE_SUCCESS;
	}
	//case where mail is marked deleted:
	if(mailNode->email.is_deleted == -88) {
		msg.header.data_length = 0;
		msg.header.opcode = OPCODE_MAIL_NOT_FOUND;
		if (send_protocol_message(client_socket, &msg))
			return STATE_ERROR;
		return STATE_SUCCESS;
	}
	
	//reminder:  from  to subject text
	msg.header.opcode = OPCODE_FROM;
	msg.header.data_length = strlen(mailNode->email.mail_sender);
	strncpy(msg.data, mailNode->email.mail_sender, MAX_USERNAME_SIZE);
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;

	msg.header.opcode = OPCODE_TO;
	msg.header.data_length = strlen(mailNode->email.mail_recievers);
	strcpy(msg.data, mailNode->email.mail_recievers);  //we can alwyas fit this in one message
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;

	msg.header.opcode = OPCODE_SUBJECT;
	msg.header.data_length = strlen(mailNode->email.subject);
	strcpy(msg.data, mailNode->email.subject);  //we can always fit this in one message
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;

	msg.header.opcode = OPCODE_TEXT;
	msg.header.data_length = strlen(mailNode->email.text);
	strcpy(msg.data, mailNode->email.text);  //we can always fit this in one message
	if (send_protocol_message(client_socket, &msg)) 
		return STATE_ERROR;

	return STATE_SUCCESS;
}


void saveUsername(const char* line){
   char copyLine[MAX_LINE_LENGTH];
   strcpy(copyLine, line);
   char password[51];
   memset(userName,0,MAX_USERNAME);     
   sscanf( copyLine, "%s %s", userName, password );
}


int check_username_passowrd_exist(const char* data, FILE* fp){
  
    char line[MAX_LINE_LENGTH];
    int line_length;
    
    while (fgets(line, sizeof(line), fp)){
	 if (line[strlen(line)-1] == '\n'){
	  line[strlen(line)-1] = '\0' ;
	 }
	 
	 line_length = strlen(line);
	 if (strncmp(line, data, line_length) == 0){ 		
	    saveUsername(line); // insert username into global variable userName so that the server knows who it's talking with
	    return STATE_SUCCESS; //found username and password
	 }
    }
    return STATE_ERROR; //didn't find username or password
}



int command_login(int client_sock, const char* data, FILE* fp){
  
    protocol_message msg;
    memset(msg.data, '\0', MAX_DATA_SIZE);  //ron added
    msg.header.data_length = 0;
    int state_flag = check_username_passowrd_exist(data, fp);
    
    if (state_flag == STATE_SUCCESS){
	msg.header.opcode = OPCODE_LOGIN_SUCCEED;
	msg.header.data_length = strnlen(SERVER_LOGIN_SUCCEED_STRING, MAX_DATA_SIZE);
	strncpy(msg.data, SERVER_LOGIN_SUCCEED_STRING, msg.header.data_length);
    }
    else{
      	msg.header.opcode = OPCODE_LOGIN_FAILED;
	msg.header.data_length = strnlen(SERVER_LOGIN_FAILED_STRING, MAX_DATA_SIZE);
	strncpy(msg.data, SERVER_LOGIN_FAILED_STRING, msg.header.data_length);
    }
      
    if (send_protocol_message(client_sock, &msg) != STATE_SUCCESS){
	return STATE_ERROR;
    }else{
	return STATE_SUCCESS;
    }

}


int server_to_client(int client_sock, char usersFilePath[]){
  
    protocol_message msg;
    memset(msg.data, '\0', MAX_DATA_SIZE);  
    int state_flag = 0;
    FILE *fp;

    msg.header.opcode = OPCODE_WELCOME;
    msg.header.data_length = strnlen(SERVER_WELCOME_STRING, MAX_DATA_SIZE);
    strncpy(msg.data, SERVER_WELCOME_STRING, msg.header.data_length);

    state_flag = send_protocol_message(client_sock, &msg);
	
    if (state_flag == STATE_ERROR){
	return STATE_ERROR;
    }
	
    while (msg.header.opcode != OPCODE_QUIT && state_flag == STATE_SUCCESS){
	
		state_flag = recv_protocol_message(client_sock, &msg);
		if (state_flag == STATE_ERROR){
			return STATE_ERROR;
		} else if (state_flag == STATE_SHUTDOWN) {
			return STATE_SHUTDOWN;
		}

		switch (msg.header.opcode){
			case OPCODE_LOGIN:	
				fp = fopen(usersFilePath , "r");
				if (fp == NULL){
					printf("Error in openning users_file: %s\n", strerror(errno));
					return STATE_ERROR;		  
				}	
				state_flag = command_login(client_sock, msg.data, fp);
				fclose(fp);
				break;
	
			case OPCODE_SHOW_INBOX:
				state_flag = handle_show_inbox(client_sock);
				break;
			case OPCODE_GET_MAIL:
				state_flag = handle_getMail(client_sock, msg.data);
				break;
			case OPCODE_COMPOSE:
				state_flag = handle_compose(client_sock, msg.data);
				break;
			case OPCODE_DELETE_MAIL:
				state_flag = handle_delete(client_sock, msg.data);
				break;
			case OPCODE_QUIT:
				break;
			default:
			state_flag = STATE_ERROR;
			break;
		}

		if (state_flag == STATE_ERROR){
			break;
		}
	}
		return state_flag;
}


int initiate_server(unsigned short int port, char usersFilePath[]){ 
    struct sockaddr_in server_addr;
    int server_sock, client_sock;
    int state_flag;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0){
	printf("Error in socket function: %s\n", strerror(errno));
	return STATE_ERROR;
    }

    memset(&server_addr, '0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
	close(server_sock);
	printf("Error in bind function: %s\n", strerror(errno));
	return STATE_ERROR;
    }

    if (listen(server_sock, 20) < 0){
	close(server_sock);
	printf("Error in listen function: %s\n", strerror(errno));
	return STATE_ERROR;
    }
	
    while (1){
        client_sock = accept(server_sock, NULL , NULL);

        if (client_sock < 0){
           printf("Error in accept function: %s\n", strerror(errno));
           continue;  
           
	}else{
	  state_flag = server_to_client(client_sock, usersFilePath);
	  if (state_flag == STATE_ERROR){
	    break;
	  
	  }else{
	      if (close(client_sock) == -1){
		  printf("Error in close function: %s\n", strerror(errno));
	      }
	  }
        }               
    }
    
        
    if (close(client_sock) == -1){
	printf("Error in close function: %s\n", strerror(errno));
    }
    
    if (close(server_sock) == -1){
	printf("Error in close function: %s\n", strerror(errno));
    }
    
    return state_flag;
}



int main(int argc, char** argv){
  
    unsigned short int port = DEFAULT_PORT;
    char usersFilePath[MAX_FILE_PATH_LENGTH+1];

    if (argc < 2 || argc > 3){
	printf("Illegal number of arguments\n");
	return STATE_ERROR;
    }
    strncpy(usersFilePath, argv[1], MAX_FILE_PATH_LENGTH);
    
    usersFilePath[MAX_FILE_PATH_LENGTH] = '\0'; 
    
    if (argc == 3){
	port = (unsigned short int) atoi(argv[2]);
    }
    
   
    makeEmptyList ();   // creates an empty list of mails

    //createListForDebug();  	
    
    if (initiate_server(port, usersFilePath) == STATE_ERROR){
	return STATE_ERROR;
    }
    return STATE_SUCCESS;

}
