#!/usr/bin/python
# -*- coding: utf-8 -*-

    
class display_array:
  def __init__(self,columns):
  
    self.column_nb = columns
    self.column_size = []
    self.left = []

    for i in range(self.column_nb):
      self.column_size.append(0)
      self.left.append(True)
      
    self.row_nb = int(0)
    self.value = []    
    self.separators = []    
    
    
  def display_top_line(self,sep):
    line=' '
    for c in range(self.column_nb):
      line=line+'__'
      for ci in range(int(self.column_size[c])):
        line=line+'_'
      if self.separators[0][c] == ' ':
        line=line+'_'
      else:  	
        line=line+' '
    line=line+sep
    return line
    
  def display_separator_line(self,row):
    line='|'
    for c in range(self.column_nb):
      line=line+'__'
      for ci in range(int(self.column_size[c])):
        line=line+'_'
      if self.separators[row][c] == ' ':
        line=line+'_'
      else:  	
        line=line+self.separators[row][c]
    return line

  def new_line(self):
    row = []
    separators = []    
    for c in range(self.column_nb):    
      row.append('')  
      separators.append('|')            
    self.value.append(row) 
    self.separators.append(separators)
    self.row_nb = int(self.row_nb)+1
    
  def separator(self):
    self.value.append(None) 
    self.separators.append(None) 
    self.row_nb = int(self.row_nb)+1

  def update_column_size(self,row,size):
    if int(self.column_size[int(row)-1]) < int(size):
      self.column_size[int(row)-1] = int(size)
            
  def set_column(self,column,value,separator=None):
    if int(column) > int(self.column_nb):
      print "set_column column number to big %d"%(column)
      exit(-1)
    self.value[int(self.row_nb)-1][int(column)-1] = value
    if separator != None:
      if separator == '':
        self.separators[int(self.row_nb)-1][int(column)-1] = ' '
      else:  
        self.separators[int(self.row_nb)-1][int(column)-1] = separator
    self.update_column_size(column,len(value))
    
  def set_column_on_right(self,column):
    if int(column) > int(self.column_nb):
      print "set_column_on_left column number to big %d"%(column)
      exit(-1)
    self.left[int(column)-1] = False
      
                	
  def display(self):
  
    topline=self.display_top_line(' ')

    print topline
    for row in range(int(self.row_nb)):
    
      if self.value[row] == None:
        separator=self.display_separator_line(row-1)
        print separator 
	continue
	
      line=''
      line=line+"| "
      for col in range(self.column_nb):
	l = self.column_size[col]-len(self.value[row][col])
	if self.left[col] == False:
      	  for i in range(l):
	    line=line+" "
	  line=line+self.value[row][col]+" "+self.separators[row][col]+" "
	else:
	  line=line+self.value[row][col]
      	  for i in range(l):
	    line=line+" "
	  line=line+" "+self.separators[row][col]+" "   	     
      print line  
          
    print separator    


#dis = display_array(6)
#dis.set_column_on_right(3)
#dis.new_line()
#for i in range(6):
#  dis.set_column(i+1,'val%d'%(i+1))
#dis.separator()
#dis.new_line()
#dis.set_column(1,'1') 
#dis.set_column(2,'22')      
#dis.set_column(3,'333')      
#dis.set_column(4,'4444')      
#dis.set_column(5,'55555')      
#dis.set_column(6,'666666')
#dis.new_line()
#dis.set_column(1,'1') 
#dis.set_column(2,'22')      
#dis.set_column(3,'333')      
#dis.set_column(4,'4444')      
#dis.set_column(5,'55555')      
#dis.set_column(6,'666666')
#dis.separator()
#dis.new_line()
#dis.set_column(3,'aqwxszedcvfr')  
#dis.display()
