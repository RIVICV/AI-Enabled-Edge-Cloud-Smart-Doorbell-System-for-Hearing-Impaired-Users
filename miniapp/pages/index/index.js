const API_BASE = 'http://114.132.168.16:5000'

Page({
  data: {
    question: '',
    answer: '',
    isOnline: true,
    recentRecords: [],
    currentTime: '',
    greetingText: '',
    todayCount: 0,
    allRecords: []
  },

  onLoad() {
    console.log('🌱 页面加载了')
    this.loadRecentRecords()
    this.updateTime()
    this.setGreeting()
    this.loadAllRecords()
    
    setInterval(() => {
      this.updateTime()
    }, 1000)
  },

  onShow() {
    this.loadRecentRecords()
    this.loadAllRecords()
  },

  updateTime() {
    const now = new Date()
    const hours = String(now.getHours()).padStart(2, '0')
    const minutes = String(now.getMinutes()).padStart(2, '0')
    this.setData({
      currentTime: hours + ':' + minutes
    })
  },

  setGreeting() {
    const hour = new Date().getHours()
    let greeting = ''
    if (hour < 6) greeting = '🌙 夜深了'
    else if (hour < 9) greeting = '🌅 早上好'
    else if (hour < 12) greeting = '☀️ 上午好'
    else if (hour < 14) greeting = '🌤️ 中午好'
    else if (hour < 18) greeting = '🌿 下午好'
    else if (hour < 21) greeting = '🌇 晚上好'
    else greeting = '🌙 夜深了'
    this.setData({ greetingText: greeting })
  },

  onInput(e) {
    this.setData({ question: e.detail.value })
  },

  askQuestion() {
    const question = this.data.question.trim()
    if (!question) {
      wx.showToast({ title: '请输入问题', icon: 'none' })
      return
    }

    wx.showLoading({ title: '🌱 AI思考中...' })

    wx.request({
      url: API_BASE + '/api/ask',
      method: 'POST',
      data: { question: question },
      success: (res) => {
        wx.hideLoading()
        if (res.data && res.data.answer) {
          this.setData({ 
            answer: res.data.answer,
            question: ''
          })
          wx.vibrateShort({ type: 'light' })
        }
      },
      fail: (err) => {
        wx.hideLoading()
        console.error('请求失败:', err)
        wx.showToast({ title: '请求失败', icon: 'none' })
      }
    })
  },

  quickAsk(e) {
    const q = e.currentTarget.dataset.q
    this.setData({ question: q })
    wx.vibrateShort({ type: 'light' })
    setTimeout(() => {
      this.askQuestion()
    }, 100)
  },

  parseDate(dateStr) {
    if (typeof dateStr === 'string' && dateStr.includes('-')) {
      dateStr = dateStr.replace(/-/g, '/')
    }
    return new Date(dateStr)
  },

  loadAllRecords() {
    const that = this
    wx.request({
      url: API_BASE + '/api/history?limit=100',
      method: 'GET',
      success: (res) => {
        if (res.data && res.data.code === 0) {
          const records = res.data.data || []
          that.setData({ allRecords: records })
          that.calculateTodayCount(records)
        }
      }
    })
  },

  calculateTodayCount(records) {
    const today = new Date()
    const todayStr = today.getFullYear() + '/' + 
                     String(today.getMonth() + 1).padStart(2, '0') + '/' + 
                     String(today.getDate()).padStart(2, '0')
    
    let count = 0
    records.forEach(record => {
      const date = this.parseDate(record.trigger_time)
      if (!isNaN(date.getTime())) {
        const dateStr = date.getFullYear() + '/' + 
                       String(date.getMonth() + 1).padStart(2, '0') + '/' + 
                       String(date.getDate()).padStart(2, '0')
        if (dateStr === todayStr) {
          count++
        }
      }
    })
    this.setData({ todayCount: count })
  },

  // ✅ 关键：直接使用服务器数据，不加 8 小时
  loadRecentRecords() {
    const that = this
    wx.request({
      url: API_BASE + '/api/history?limit=5',
      method: 'GET',
      success: (res) => {
        if (res.data && res.data.code === 0) {
          that.setData({ recentRecords: res.data.data || [] })
          console.log('📋 服务器返回的时间:', res.data.data)
        }
      },
      fail: () => {
        console.log('加载历史记录失败')
      }
    })
  },

  goHistory() {
    wx.navigateTo({ url: '/pages/history/history' })
  }
})